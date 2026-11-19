package android.content;

import android.content.res.Configuration;

/**
 * 组件回调接口（AOSP android.content.ComponentCallbacks；§31.7 C 档 lib 接口面）。
 */
public interface ComponentCallbacks {

    void onConfigurationChanged(Configuration newConfig);

    void onLowMemory();
}
