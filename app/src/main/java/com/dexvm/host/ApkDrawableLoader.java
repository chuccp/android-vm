package com.dexvm.host;

import android.content.Context;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.content.res.loader.ResourcesLoader;
import android.content.res.loader.ResourcesProvider;
import android.graphics.drawable.Drawable;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.util.DisplayMetrics;
import android.util.Log;

import java.io.File;
import java.util.HashMap;
import java.util.Map;

/**
 * guest APK 资源 drawable 装载器（桥接出画：XML vector / bitmap / layer-list）。
 *
 * <p>VM 在 Resources.getDrawable 时登记 drawable -> resId，桥接快照把该 resId 投影到
 * 宿主的 ImageView 节点。宿主用公开 API 30+ 的 ResourcesProvider/ResourcesLoader
 * 把 guest APK 的资源表挂到独立 Resources 上，再按 resId 装载真实 Drawable
 * （Framework 的 VectorDrawable/BitmapDrawable 光栅化，不依赖 VM 的 Canvas 缝）。
 *
 * <p>资源 id 与宿主可能同段（都为 0x7f），但 provider 先于 base 资源表查询；
 * 只要宿主自身没有同 id 条目（通常远小于 guest 的 0x7f08xxxx 段），即可命中
 * guest 资源。
 */
final class ApkDrawableLoader {
    private static final String TAG = "DexVmDrawableHost";

    private final Context context;
    private final String apkPath;
    private Resources resources;
    private ParcelFileDescriptor pfd;
    private ResourcesProvider provider;
    private final Map<Integer, Drawable> cache = new HashMap<>();
    private boolean failed;

    ApkDrawableLoader(Context context, String apkPath) {
        this.context = context.getApplicationContext();
        this.apkPath = apkPath;
    }

    Drawable load(int resId) {
        if (resId == 0 || failed) return null;
        Drawable cached = cache.get(resId);
        if (cached != null) return cached;
        ensureResources();
        if (resources == null) return null;
        try {
            Drawable d = resources.getDrawable(resId, context.getTheme());
            if (d != null) {
                cache.put(resId, d);
                if (cache.size() <= 24) {
                    Log.i(TAG, "load 0x" + Integer.toHexString(resId) + " -> "
                            + d.getClass().getSimpleName() + " "
                            + d.getIntrinsicWidth() + "x" + d.getIntrinsicHeight());
                }
            }
            return d;
        } catch (Throwable t) {
            Log.w(TAG, "load 0x" + Integer.toHexString(resId) + " failed: " + t);
            return null;
        }
    }

    private void ensureResources() {
        if (resources != null || failed) return;
        if (Build.VERSION.SDK_INT < 30) {
            failed = true;
            return;
        }
        Resources hostRes = context.getResources();
        DisplayMetrics dm = new DisplayMetrics();
        dm.setTo(hostRes.getDisplayMetrics());
        Configuration cfg = new Configuration(hostRes.getConfiguration());
        // 首选公开 API：ResourcesProvider 直接挂 guest APK 资源表。
        try {
            pfd = ParcelFileDescriptor.open(new File(apkPath),
                    ParcelFileDescriptor.MODE_READ_ONLY);
            provider = ResourcesProvider.loadFromTable(pfd, null);
            ResourcesLoader loader = new ResourcesLoader();
            loader.addProvider(provider);
            resources = new Resources(context.getAssets(), dm, cfg);
            resources.addLoaders(loader);
            return;
        } catch (Throwable t) {
            Log.w(TAG, "ResourcesProvider load failed", t);
            closeQuietly();
        }
        // 兜底：隐藏 API addAssetPath（未打包 resources 的 APK/怪表结构面）。
        try {
            Class<?> amc = Class.forName("android.content.res.AssetManager");
            java.lang.reflect.Constructor<?> ctor = amc.getDeclaredConstructor();
            ctor.setAccessible(true);
            android.content.res.AssetManager am =
                    (android.content.res.AssetManager) ctor.newInstance();
            java.lang.reflect.Method add = amc.getDeclaredMethod("addAssetPath", String.class);
            add.setAccessible(true);
            Object cookie = add.invoke(am, apkPath);
            if (cookie instanceof Integer && (Integer) cookie == 0) {
                throw new IllegalStateException("addAssetPath cookie=0");
            }
            resources = new Resources(am, dm, cfg);
            Log.i(TAG, "guest resource table via addAssetPath");
        } catch (Throwable t) {
            failed = true;
            Log.w(TAG, "guest resource table unavailable", t);
        }
    }

    private void closeQuietly() {
        try {
            if (pfd != null) pfd.close();
        } catch (Throwable ignored) {
        }
        pfd = null;
    }
}
