package com.coke.target;

import android.app.Application;
import android.content.Context;
import android.util.Log;

/**
 * 生命周期 fixture（旧 guest 考卷 Application 继任）：静态 {@code lifecycleLog} 字段名
 * 契约（runtime_contract NativeRunApkLifecycle 按字段名读取；勿改字段名）。
 * token 语义同旧考卷先例：attachBaseContext; / onCreate;（Application）。
 */
public class RuntimeLifecycleApp extends Application {
    public static String lifecycleLog = "";

    @Override
    protected void attachBaseContext(Context base) {
        super.attachBaseContext(base);
        lifecycleLog += "attachBaseContext;";
        Log.i("DexVmTargetLifecycle", "attachBaseContext");
    }

    @Override
    public void onCreate() {
        super.onCreate();
        lifecycleLog += "onCreate;";
        Log.i("DexVmTargetLifecycle", "Application.onCreate");
    }
}
