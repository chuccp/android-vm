package com.dexvm.pitest;

import android.app.Application;
import android.content.Context;

/** 生命周期序观测：provider 链推进到 Application.onCreate 的锚点。 */
public class ProbeApp extends Application {
    public static String lifecycleLog = "";

    @Override
    protected void attachBaseContext(Context base) {
        super.attachBaseContext(base);
        lifecycleLog += "attachBaseContext;";
    }

    @Override
    public void onCreate() {
        super.onCreate();
        lifecycleLog += "onCreate;";
    }
}
