package com.contactvirgilio.melasa;

import android.os.Bundle;
import com.getcapacitor.BridgeActivity;

public class MainActivity extends BridgeActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        registerPlugin(MelasaAudioPlugin.class);
        super.onCreate(savedInstanceState);
    }
}
