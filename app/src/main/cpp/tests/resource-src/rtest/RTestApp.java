package com.dexvm.rtest;

import android.app.Application;

/** 生命周期序记录（provider 链后 Application.onCreate 收口断言）。 */
public class RTestApp extends Application {
    public static String lifecycleLog = "";

    @Override
    protected void attachBaseContext(android.content.Context base) {
        super.attachBaseContext(base);
        lifecycleLog += "attachBaseContext;";
    }

    @Override
    public void onCreate() {
        super.onCreate();
        lifecycleLog += "onCreate;";
    }
}
