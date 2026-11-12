package com.contactvirgilio.melasa;

import android.view.View;
import android.webkit.WebView;
import com.getcapacitor.Plugin;
import com.getcapacitor.annotation.CapacitorPlugin;

/** Exposes the native sound engine to the page as window.MelasaNative and ties it to the app lifecycle. */
@CapacitorPlugin(name = "MelasaAudio")
public class MelasaAudioPlugin extends Plugin {
    private NativeAudio audio;

    @Override
    public void load() {
        WebView web = bridge.getWebView();
        audio = new NativeAudio(getContext().getApplicationContext(), web);
        web.addJavascriptInterface(audio, "MelasaNative");
        // A finger resting on a pad must never turn into a long-press, a buzz or an overscroll glow
        web.setLongClickable(false);
        web.setOnLongClickListener(v -> true);
        web.setHapticFeedbackEnabled(false);
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
    }

    @Override
    protected void handleOnPause() { if (audio != null) audio.onPause(); }

    @Override
    protected void handleOnResume() { if (audio != null) audio.onResume(); }

    @Override
    protected void handleOnDestroy() { if (audio != null) audio.onPause(); }
}
