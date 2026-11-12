// Melasa native sound engine: a small sample mixer on top of Oboe (low-latency Android audio).
// Samples are decoded in Java and handed over as 16-bit mono PCM. play/release commands
// arrive from the WebView's JavaScript bridge thread through a lock-free queue, so the
// audio callback never waits on anything.
#include <jni.h>
#include <oboe/Oboe.h>
#include <android/log.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MelasaAudio", __VA_ARGS__)

namespace {

constexpr int kMaxSamples = 2048;
constexpr int kMaxVoices = 48;
constexpr uint32_t kQueue = 256;
constexpr int kChunk = 1024;

struct Sample {
    std::atomic<const int16_t*> data{nullptr};   // points one frame into a zero-padded buffer
    std::atomic<int> frames{0};
    std::atomic<int> rate{44100};
};
Sample gSamples[kMaxSamples];

struct Cmd { int type; int voice; int sample; float gain; float rate; float seconds; };
enum { kPlay = 1, kRelease = 2 };

struct Voice {
    bool active = false;
    int id = 0;
    const int16_t* data = nullptr;
    int frames = 0;
    double pos = 0, step = 1;
    float gain = 0, env = 1, envMul = 1;
    bool releasing = false;
    uint64_t born = 0;
};

class Engine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    bool start() {
        std::lock_guard<std::mutex> lock(streamLock_);
        wantRunning_ = true;
        return openLocked();
    }

    void stop() {
        std::lock_guard<std::mutex> lock(streamLock_);
        wantRunning_ = false;
        closeLocked();
    }

    bool push(const Cmd& c) {
        std::lock_guard<std::mutex> lock(pushLock_);
        const uint32_t h = head_.load(std::memory_order_relaxed);
        const uint32_t next = (h + 1) % kQueue;
        if (next == tail_.load(std::memory_order_acquire)) return false;   // full: stream not running
        queue_[h] = c;
        head_.store(next, std::memory_order_release);
        return true;
    }

    double latencyMs() {
        std::lock_guard<std::mutex> lock(streamLock_);
        if (!stream_) return -1;
        auto r = stream_->calculateLatencyMillis();
        if (r && r.value() > 0 && r.value() < 1000) return r.value();
        return 1000.0 * stream_->getBufferSizeInFrames() / std::max(1, stream_->getSampleRate());
    }

    void info(char* out, size_t n) {
        std::lock_guard<std::mutex> lock(streamLock_);
        if (!stream_) { snprintf(out, n, "closed"); return; }
        snprintf(out, n, "rate=%d burst=%d buffer=%d api=%s sharing=%s",
                 stream_->getSampleRate(), stream_->getFramesPerBurst(), stream_->getBufferSizeInFrames(),
                 oboe::convertToText(stream_->getAudioApi()), oboe::convertToText(stream_->getSharingMode()));
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* s, void* audioData, int32_t numFrames) override {
        drain();
        const int ch = s->getChannelCount();
        const bool isFloat = s->getFormat() == oboe::AudioFormat::Float;
        int done = 0;
        while (done < numFrames) {
            const int n = std::min(numFrames - done, kChunk);
            mixChunk(n);
            if (isFloat) {
                float* o = static_cast<float*>(audioData) + static_cast<size_t>(done) * ch;
                for (int i = 0; i < n; i++) for (int c = 0; c < ch; c++) *o++ = mix_[i];
            } else {
                int16_t* o = static_cast<int16_t*>(audioData) + static_cast<size_t>(done) * ch;
                for (int i = 0; i < n; i++) { const int16_t y = static_cast<int16_t>(mix_[i] * 32767.0f); for (int c = 0; c < ch; c++) *o++ = y; }
            }
            done += n;
        }
        return oboe::DataCallbackResult::Continue;
    }

    void onErrorAfterClose(oboe::AudioStream*, oboe::Result error) override {
        LOGI("stream closed: %s", oboe::convertToText(error));
        std::lock_guard<std::mutex> lock(streamLock_);
        stream_.reset();
        if (wantRunning_) openLocked();   // e.g. headphones plugged in or out
    }

private:
    bool openLocked() {
        if (stream_) return true;
        oboe::AudioStreamBuilder b;
        b.setDirection(oboe::Direction::Output)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(oboe::ChannelCount::Stereo)
            ->setUsage(oboe::Usage::Game)
            ->setContentType(oboe::ContentType::Music)
            ->setDataCallback(this)
            ->setErrorCallback(this);
        oboe::Result r = b.openStream(stream_);
        if (r != oboe::Result::OK) { LOGI("open failed: %s", oboe::convertToText(r)); stream_.reset(); return false; }
        stream_->setBufferSizeInFrames(stream_->getFramesPerBurst() * 2);
        streamRate_.store(stream_->getSampleRate());
        r = stream_->requestStart();
        if (r != oboe::Result::OK) { LOGI("start failed: %s", oboe::convertToText(r)); stream_->close(); stream_.reset(); return false; }
        LOGI("stream open rate=%d burst=%d buffer=%d api=%s", stream_->getSampleRate(), stream_->getFramesPerBurst(),
             stream_->getBufferSizeInFrames(), oboe::convertToText(stream_->getAudioApi()));
        return true;
    }

    void closeLocked() {
        if (!stream_) return;
        stream_->stop();
        stream_->close();
        stream_.reset();
    }

    // ---- audio thread only below ----
    void drain() {
        uint32_t t = tail_.load(std::memory_order_relaxed);
        const uint32_t h = head_.load(std::memory_order_acquire);
        while (t != h) { handle(queue_[t]); t = (t + 1) % kQueue; }
        tail_.store(t, std::memory_order_release);
    }

    void handle(const Cmd& c) {
        const float sr = static_cast<float>(streamRate_.load(std::memory_order_relaxed));
        if (c.type == kPlay) {
            if (c.sample < 0 || c.sample >= kMaxSamples) return;
            const int16_t* d = gSamples[c.sample].data.load(std::memory_order_acquire);
            if (!d) return;
            Voice* v = nullptr;
            for (auto& x : voices_) if (!x.active) { v = &x; break; }
            if (!v) {   // all busy: take a fading voice if there is one, otherwise the oldest
                for (auto& x : voices_) if (!v || (x.releasing && !v->releasing) || (x.releasing == v->releasing && x.born < v->born)) v = &x;
            }
            v->active = true; v->id = c.voice; v->data = d;
            v->frames = gSamples[c.sample].frames.load(std::memory_order_relaxed);
            v->pos = 0;
            v->step = static_cast<double>(c.rate) * gSamples[c.sample].rate.load(std::memory_order_relaxed) / sr;
            v->gain = c.gain / 32768.0f; v->env = 1; v->envMul = 1; v->releasing = false;
            v->born = ++clock_;
        } else if (c.type == kRelease) {
            for (auto& x : voices_) if (x.active && x.id == c.voice) {
                x.releasing = true;
                x.envMul = std::pow(0.001f, 1.0f / (std::max(0.005f, c.seconds) * sr));   // -60 dB after `seconds`
            }
        }
    }

    void mixChunk(int n) {
        std::memset(mix_, 0, sizeof(float) * n);
        for (auto& v : voices_) {
            if (!v.active) continue;
            for (int i = 0; i < n; i++) {
                const int ip = static_cast<int>(v.pos);
                if (ip >= v.frames) { v.active = false; break; }
                const float f = static_cast<float>(v.pos - ip);
                const int16_t* p = v.data + ip;   // p[-1]..p[2] are valid thanks to padding
                const float y0 = p[-1], y1 = p[0], y2 = p[1], y3 = p[2];
                const float c1 = 0.5f * (y2 - y0);
                const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                mix_[i] += (((c3 * f + c2) * f + c1) * f + y1) * v.gain * v.env;
                if (v.releasing) { v.env *= v.envMul; if (v.env < 0.0005f) { v.active = false; break; } }
                v.pos += v.step;
            }
        }
        // Master: same curve as the web engine (gain 0.8 into a tanh soft clipper)
        for (int i = 0; i < n; i++) {
            const float y = std::tanh(mix_[i] * 0.8f) * 1.3130353f;
            mix_[i] = y > 1.0f ? 1.0f : (y < -1.0f ? -1.0f : y);
        }
    }

    std::mutex streamLock_, pushLock_;
    std::shared_ptr<oboe::AudioStream> stream_;
    bool wantRunning_ = false;
    std::atomic<int> streamRate_{48000};
    Cmd queue_[kQueue];
    std::atomic<uint32_t> head_{0}, tail_{0};
    Voice voices_[kMaxVoices];
    uint64_t clock_ = 0;
    float mix_[kChunk];
};

Engine gEngine;

}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL Java_com_contactvirgilio_melasa_NativeAudio_nativeStart(JNIEnv*, jclass) {
    return gEngine.start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_contactvirgilio_melasa_NativeAudio_nativeStop(JNIEnv*, jclass) {
    gEngine.stop();
}

JNIEXPORT void JNICALL Java_com_contactvirgilio_melasa_NativeAudio_nativeSetSample(JNIEnv* env, jclass, jint id, jshortArray data, jint frames, jint rate) {
    if (id < 0 || id >= kMaxSamples || frames <= 0 || env->GetArrayLength(data) < frames) return;
    auto* buf = static_cast<int16_t*>(std::calloc(static_cast<size_t>(frames) + 4, sizeof(int16_t)));
    if (!buf) return;
    env->GetShortArrayRegion(data, 0, frames, buf + 1);
    gSamples[id].frames.store(frames);
    gSamples[id].rate.store(rate > 0 ? rate : 44100);
    gSamples[id].data.store(buf + 1, std::memory_order_release);   // kept for the life of the app
}

JNIEXPORT void JNICALL Java_com_contactvirgilio_melasa_NativeAudio_nativePlay(JNIEnv*, jclass, jint voice, jint sample, jfloat gain, jfloat rate) {
    gEngine.push(Cmd{kPlay, voice, sample, gain, rate > 0 ? rate : 1.0f, 0});
}

JNIEXPORT void JNICALL Java_com_contactvirgilio_melasa_NativeAudio_nativeRelease(JNIEnv*, jclass, jint voice, jfloat seconds) {
    gEngine.push(Cmd{kRelease, voice, 0, 0, 1, seconds});
}

JNIEXPORT jdouble JNICALL Java_com_contactvirgilio_melasa_NativeAudio_nativeLatency(JNIEnv*, jclass) {
    return gEngine.latencyMs();
}

JNIEXPORT jstring JNICALL Java_com_contactvirgilio_melasa_NativeAudio_nativeInfo(JNIEnv* env, jclass) {
    char buf[200];
    gEngine.info(buf, sizeof(buf));
    return env->NewStringUTF(buf);
}

}  // extern "C"
