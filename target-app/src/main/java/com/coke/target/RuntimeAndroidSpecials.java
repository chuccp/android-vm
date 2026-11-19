package com.coke.target;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import java.util.Locale;

/**
 * Android-only framework paths that are normally hidden behind the SDK:
 * typed Bundle/Intent extras, virtual activity configuration and the decor
 * view/window relationship.
 */
public final class RuntimeAndroidSpecials {
    private RuntimeAndroidSpecials() {
    }

    public static String probe(Context context) {
        Bundle state = new Bundle();
        state.putString("mode", "vm");
        state.putInt("count", 9);
        state.putLong("stamp", 1L << 34);
        state.putBoolean("armed", true);
        state.putFloat("ratio", 1.25f);

        int bundleScore = 0;
        if ("vm".equals(state.getString("mode"))) {
            bundleScore += 1;
        }
        if (state.getInt("count") == 9) {
            bundleScore += 2;
        }
        if (state.getLong("stamp") == (1L << 34)) {
            bundleScore += 4;
        }
        if (state.getBoolean("armed")) {
            bundleScore += 8;
        }
        if (state.getFloat("ratio") == 1.25f) {
            bundleScore += 16;
        }
        if (state.size() == 5 && state.containsKey("mode")) {
            bundleScore += 32;
        }

        Intent intent = new Intent();
        intent.putExtra("count", 7);
        intent.putExtra("enabled", true);
        intent.putExtra("stamp", 11L);
        intent.putExtra("label", "dex");
        int intentScore = 0;
        if (intent.getIntExtra("count", -1) == 7) {
            intentScore += 1;
        }
        if (intent.getBooleanExtra("enabled", false)) {
            intentScore += 2;
        }
        if (intent.getLongExtra("stamp", -1L) == 11L) {
            intentScore += 4;
        }
        if ("dex".equals(intent.getStringExtra("label"))) {
            intentScore += 8;
        }
        state.remove("count");
        if (state.getInt("count", -1) == -1) {
            bundleScore += 64;
        }

        Configuration configuration = context.getResources().getConfiguration();
        String configurationText = configuration.toString();
        Locale locale = Locale.getDefault();
        Activity activity = (Activity) context;
        View decor = activity.getWindow().getDecorView();
        decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
        String window = decor != null ? "decor-ready" : "decor-missing";

        return "Android specials: bundle=" + bundleScore
                + " intent=" + intentScore
                + " sdk=" + Build.VERSION.SDK_INT
                + " locale=" + locale.getLanguage()
                + " config=" + configurationText
                + " window=" + window;
    }
}
