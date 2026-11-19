package android.view;

/**
 * android.view.WindowInsetsController（AOSP API 30 公开接口）。
 *
 * <p>VM 无系统栏控制通道：实现类 {@link WindowInsetsControllerImpl} 为
 * no-op 面（androidx WindowInsetsControllerCompat 的消费点可安全调用）。
 */
public interface WindowInsetsController {
    int APPEARANCE_LIGHT_STATUS_BARS = 8;
    int APPEARANCE_LIGHT_NAVIGATION_BARS = 16;
    int BEHAVIOR_SHOW_BARS_BY_TOUCH = 0;
    int BEHAVIOR_SHOW_BARS_BY_SWIPE = 1;
    int BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE = 2;
    int TYPE_STATUS_BARS = 1;
    int TYPE_NAVIGATION_BARS = 2;
    int TYPE_CAPTION_BAR = 4;
    int TYPE_IME = 8;
    int TYPE_SYSTEM_BARS = TYPE_STATUS_BARS | TYPE_NAVIGATION_BARS;

    interface OnControllableInsetsChangedListener {
        void onControllableInsetsChanged(WindowInsetsController controller, int typeMask);
    }

    void show(int types);

    void hide(int types);

    int getSystemBarsBehavior();

    void setSystemBarsBehavior(int behavior);

    void setSystemBarsAppearance(int appearance, int mask);

    int getSystemBarsAppearance();

    void addOnControllableInsetsChangedListener(OnControllableInsetsChangedListener listener);

    void removeOnControllableInsetsChangedListener(OnControllableInsetsChangedListener listener);
}
