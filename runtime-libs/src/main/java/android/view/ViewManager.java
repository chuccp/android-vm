package android.view;

/**
 * 视图管理器接口（AOSP android.view.ViewManager；§31.7 C 档 lib 接口面）。
 */
public interface ViewManager {
    void addView(View view, ViewGroup.LayoutParams params);

    void updateViewLayout(View view, ViewGroup.LayoutParams params);

    void removeView(View view);
}
