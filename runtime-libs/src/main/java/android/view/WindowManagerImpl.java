package android.view;

/**
 * VM 窗口管理器实现（AOSP WindowManagerImpl 公开面骨架）。
 *
 * <p>窗口操作由宿主渲染桥承接；本类提供 AOSP 恒非空的
 * getDefaultDisplay() 度量面（guest 窗口 540x960 / density 2.0），
 * 避免 app 在构造视图时对 getSystemService("window") 的结果 NPE。
 */
public class WindowManagerImpl implements WindowManager {

    private static WindowManagerImpl sInstance;

    private final Display mDisplay = new Display(540, 960);

    public WindowManagerImpl() {
    }

    public static WindowManagerImpl getInstance() {
        if (sInstance == null) {
            sInstance = new WindowManagerImpl();
        }
        return sInstance;
    }

    @Override
    public Display getDefaultDisplay() {
        return mDisplay;
    }

    @Override
    public void addView(View view, ViewGroup.LayoutParams params) {
    }

    @Override
    public void updateViewLayout(View view, ViewGroup.LayoutParams params) {
    }

    @Override
    public void removeView(View view) {
    }

    @Override
    public void removeViewImmediate(View view) {
    }

    @Override
    public void requestAppKeyboardShortcuts(KeyboardShortcutsReceiver receiver, int deviceId) {
    }
}
