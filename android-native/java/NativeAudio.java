package com.contactvirgilio.melasa;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.util.Base64;
import android.util.Log;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

/** JavaScript-facing side of the native engine. Methods run on the WebView's bridge thread. */
public class NativeAudio {
    private static final String TAG = "MelasaAudio";
    private static boolean libOk;

    static {
        try { System.loadLibrary("melasa"); libOk = true; }
        catch (Throwable t) { libOk = false; Log.e(TAG, "native library missing", t); }
    }

    private final Context ctx;
    private final WebView web;
    private final ExecutorService pool = Executors.newFixedThreadPool(3);
    private final AtomicInteger voiceIds = new AtomicInteger(0);
    private volatile boolean started;

    NativeAudio(Context ctx, WebView web) { this.ctx = ctx; this.web = web; }

    void onPause() { if (libOk && started) nativeStop(); }
    void onResume() { if (libOk && started) nativeStart(); }

    @JavascriptInterface
    public boolean start() {
        if (!libOk) return false;
        try { started = nativeStart(); } catch (Throwable t) { Log.e(TAG, "start", t); started = false; }
        return started;
    }

    /** list: one "id<TAB>path" per line, paths relative to the web root. Calls window.melasaNativeLoaded(token, ok, total). */
    @JavascriptInterface
    public void load(final String token, String list) {
        final String[] lines = list.split("\n");
        final AtomicInteger left = new AtomicInteger(lines.length), ok = new AtomicInteger(0);
        for (final String line : lines) {
            pool.execute(() -> {
                try {
                    int tab = line.indexOf('\t');
                    int id = Integer.parseInt(line.substring(0, tab));
                    int[] rate = new int[1];
                    short[] pcm = decode(line.substring(tab + 1), rate);
                    if (pcm.length > 0) { nativeSetSample(id, pcm, pcm.length, rate[0]); ok.incrementAndGet(); }
                } catch (Throwable t) {
                    Log.e(TAG, "decode failed: " + line, t);
                }
                if (left.decrementAndGet() == 0) {
                    final String safe = token.replaceAll("[^A-Za-z0-9]", "");
                    Log.i(TAG, "loaded " + ok.get() + "/" + lines.length);
                    web.post(() -> web.evaluateJavascript(
                        "window.melasaNativeLoaded&&window.melasaNativeLoaded('" + safe + "'," + ok.get() + "," + lines.length + ")", null));
                }
            });
        }
    }

    /** 16-bit little-endian mono PCM, base64 (used for the synth kit rendered in the page). */
    @JavascriptInterface
    public void loadPcm(int id, String base64, int rate) {
        if (!libOk) return;
        byte[] raw = Base64.decode(base64, Base64.DEFAULT);
        short[] pcm = new short[raw.length / 2];
        ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(pcm);
        if (pcm.length > 0) nativeSetSample(id, pcm, pcm.length, rate);
    }

    @JavascriptInterface
    public int play(int sample, float gain, float rate) {
        int voice = voiceIds.incrementAndGet();
        nativePlay(voice, sample, gain, rate);
        return voice;
    }

    @JavascriptInterface
    public void release(int voice, float seconds) { nativeRelease(voice, seconds); }

    @JavascriptInterface
    public double latencyMs() { return libOk ? nativeLatency() : -1; }

    @JavascriptInterface
    public String info() { return libOk ? nativeInfo() : "unavailable"; }

    /** Decodes an audio asset to mono 16-bit PCM and drops any silence before the attack. */
    private short[] decode(String path, int[] rateOut) throws IOException {
        MediaExtractor ex = new MediaExtractor();
        MediaCodec codec = null;
        AssetFileDescriptor afd = null;
        File tmp = null;
        try {
            try {
                afd = ctx.getAssets().openFd("public/" + path);
                ex.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
            } catch (IOException compressed) {   // asset stored compressed: copy it out first
                tmp = File.createTempFile("smp", ".bin", ctx.getCacheDir());
                try (InputStream in = ctx.getAssets().open("public/" + path); FileOutputStream out = new FileOutputStream(tmp)) {
                    byte[] buf = new byte[16384];
                    for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
                }
                ex.setDataSource(tmp.getAbsolutePath());
            }
            MediaFormat fmt = null;
            for (int i = 0; i < ex.getTrackCount(); i++) {
                MediaFormat f = ex.getTrackFormat(i);
                String mime = f.getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("audio/")) { fmt = f; ex.selectTrack(i); break; }
            }
            if (fmt == null) throw new IOException("no audio track in " + path);
            int rate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE);
            int channels = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
            codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME));
            codec.configure(fmt, null, null, 0);
            codec.start();

            ByteArrayOutputStream bytes = new ByteArrayOutputStream(1 << 18);
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            boolean inDone = false, outDone = false;
            for (int guard = 0; !outDone && guard < 200000; guard++) {
                if (!inDone) {
                    int in = codec.dequeueInputBuffer(5000);
                    if (in >= 0) {
                        ByteBuffer b = codec.getInputBuffer(in);
                        int n = b == null ? -1 : ex.readSampleData(b, 0);
                        if (n < 0) { codec.queueInputBuffer(in, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inDone = true; }
                        else { codec.queueInputBuffer(in, 0, n, ex.getSampleTime(), 0); ex.advance(); }
                    }
                }
                int o = codec.dequeueOutputBuffer(info, 5000);
                if (o >= 0) {
                    ByteBuffer b = codec.getOutputBuffer(o);
                    if (b != null && info.size > 0) {
                        byte[] chunk = new byte[info.size];
                        b.position(info.offset); b.limit(info.offset + info.size); b.get(chunk);
                        bytes.write(chunk, 0, chunk.length);
                    }
                    codec.releaseOutputBuffer(o, false);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) outDone = true;
                } else if (o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    MediaFormat of = codec.getOutputFormat();
                    if (of.containsKey(MediaFormat.KEY_SAMPLE_RATE)) rate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE);
                    if (of.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) channels = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
                }
            }

            byte[] raw = bytes.toByteArray();
            short[] all = new short[raw.length / 2];
            ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(all);
            if (channels < 1) channels = 1;
            int frames = all.length / channels;
            short[] mono = all;
            if (channels > 1) {
                mono = new short[frames];
                for (int i = 0; i < frames; i++) { int sum = 0; for (int c = 0; c < channels; c++) sum += all[i * channels + c]; mono[i] = (short) (sum / channels); }
            }
            // Start just before the attack: skip everything under 1% of the early peak
            int look = Math.min(frames, rate / 10), peak = 0;
            for (int i = 0; i < look; i++) peak = Math.max(peak, Math.abs(mono[i]));
            int first = 0;
            while (first < frames && Math.abs(mono[first]) < peak / 100 + 1) first++;
            first = Math.max(0, Math.min(first, look) - 24);
            short[] out = new short[frames - first];
            System.arraycopy(mono, first, out, 0, out.length);
            rateOut[0] = rate;
            return out;
        } finally {
            if (codec != null) { try { codec.stop(); } catch (Throwable ignored) {} codec.release(); }
            ex.release();
            if (afd != null) { try { afd.close(); } catch (IOException ignored) {} }
            if (tmp != null) tmp.delete();
        }
    }

    private static native boolean nativeStart();
    private static native void nativeStop();
    private static native void nativeSetSample(int id, short[] data, int frames, int rate);
    private static native void nativePlay(int voice, int sample, float gain, float rate);
    private static native void nativeRelease(int voice, float seconds);
    private static native double nativeLatency();
    private static native String nativeInfo();
}
