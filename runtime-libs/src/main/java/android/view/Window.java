package android.view;
import android.os.IBinder;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.view.KeyEvent;
import android.view.MotionEvent;

/**
 * 窗口（AOSP android.view.Window 公开面；§31.7 C 档 lib 骨架——窗口归属/样式
 * 字段态 + 请求面；真窗口系统归桥接层）。
 */
public class Window {

    private WindowInsetsController mInsetsController;

    // ---- VM 单窗口桥接面（Dialog 内容树经快照导出 -> 宿主物化叠加）----
    // AOSP 真实 WindowManager 归桥接层：这里保存内容树与窗口布局意图，
    // Dialog.show/dismiss 经 VmLayoutHost 通知 VM 登记/摘除窗口根。
    private View mContentView;
    private int mLayoutWidth = ViewGroup.LayoutParams.WRAP_CONTENT;
    private int mLayoutHeight = ViewGroup.LayoutParams.WRAP_CONTENT;
    private int mLayoutGravity = -1;
    private float mDimAmount = 0.32f;
    private Drawable mBackgroundDrawable;

    /**
     * AOSP API 30：系统栏外观/行为控制器（VM 面 no-op 单例）。
     * 惰性创建：VM 的 Window 实例由 AllocObject 直接分配、不跑 <init>，
     * 字段初始化器不会执行——首次取用时就地补。
     */
    public WindowInsetsController getInsetsController() {
        if (mInsetsController == null) {
            mInsetsController = new WindowInsetsControllerImpl();
        }
        return mInsetsController;
    }

    /** 窗口回调（AOSP Window.Callback）。 */
    public interface Callback {
        boolean dispatchKeyEvent(KeyEvent event);

        boolean dispatchKeyShortcutEvent(KeyEvent event);

        boolean dispatchTouchEvent(MotionEvent event);

        boolean dispatchTrackballEvent(MotionEvent event);

        boolean dispatchGenericMotionEvent(MotionEvent event);

        boolean dispatchPopulateAccessibilityEvent(android.view.accessibility.AccessibilityEvent event);

        View onCreatePanelView(int featureId);

        boolean onCreatePanelMenu(int featureId, Menu menu);

        boolean onPreparePanel(int featureId, View view, Menu menu);

        boolean onMenuOpened(int featureId, Menu menu);

        boolean onMenuItemSelected(int featureId, MenuItem item);

        void onWindowAttributesChanged(WindowManager.LayoutParams attrs);

        void onContentChanged();

        void onWindowFocusChanged(boolean hasFocus);

        void onAttachedToWindow();

        void onDetachedFromWindow();

        void onPanelClosed(int featureId, Menu menu);

        boolean onSearchRequested();

        boolean onSearchRequested(android.view.accessibility.AccessibilityEvent searchEvent);

        android.view.ActionMode onWindowStartingActionMode(ActionMode.Callback callback);

        void onActionModeStarted(ActionMode mode);

        void onActionModeFinished(ActionMode mode);
    }

    public static final int FEATURE_OPTIONS_PANEL = 0;
    public static final int FEATURE_NO_TITLE = 1;
    public static final int FEATURE_PROGRESS = 2;
    public static final int FEATURE_INDETERMINATE_PROGRESS = 3;
    public static final int FEATURE_LEFT_ICON = 4;
    public static final int FEATURE_RIGHT_ICON = 5;
    public static final int FEATURE_ACTION_BAR = 8;
    public static final int FEATURE_ACTION_BAR_OVERLAY = 9;
    public static final int FEATURE_ACTION_MODE_OVERLAY = 10;

    public static final int ID_ANDROID_CONTENT = 0x01020002;

    private final Context mContext;
    private Callback mCallback;
    private WindowManager mWindowManager;
    private CharSequence mTitle;

    public Window(Context context) {
        this.mContext = context;
    }

    public final Context getContext() {
        return this.mContext;
    }

    public void setWindowManager(WindowManager wm, IBinder appToken, String appName,
                                 boolean hardwareAccelerated) {
        this.mWindowManager = wm;
    }

    public WindowManager getWindowManager() {
        return this.mWindowManager;
    }

    public void setCallback(Callback callback) {
        this.mCallback = callback;
    }

    public Callback getCallback() {
        return this.mCallback;
    }

    public void setChildDrawable(int featureId, Drawable drawable) {
    }

    public void setChildInt(int featureId, int value) {
    }

    public boolean hasFeature(int feature) {
        return false;
    }

    public void requestFeature(int featureId) {
    }

    public void setFlags(int flags, int mask) {
        WindowManager.LayoutParams attrs = getAttributes();
        attrs.flags = (attrs.flags & ~mask) | (flags & mask);
    }

    public void addFlags(int flags) {
        getAttributes().flags |= flags;
    }

    public void clearFlags(int flags) {
        getAttributes().flags &= ~flags;
    }

    public void setStatusBarColor(int color) {
    }

    public void setNavigationBarColor(int color) {
    }

    public void setTitle(CharSequence title) {
        this.mTitle = title;
    }

    public void setTitle(int titleId) {
    }

    public CharSequence getTitle() {
        return this.mTitle;
    }

    public View getDecorView() {
        return mContentView;
    }

    public View peekDecorView() {
        return mContentView;
    }

    public void setContentView(View view) {
        this.mContentView = view;
    }

    public void setContentView(int layoutResID) {
        View view = LayoutInflater.from(getContext()).inflate(layoutResID, null);
        if (view != null) setContentView(view);
    }

    public void addContentView(View view, ViewGroup.LayoutParams params) {
        if (mContentView instanceof ViewGroup) {
            ((ViewGroup) mContentView).addView(view, params);
        } else if (mContentView == null) {
            mContentView = view;
        }
    }

    public void setBackgroundDrawable(Drawable d) {
        this.mBackgroundDrawable = d;
    }

    public Drawable getBackgroundDrawableForVm() {
        return mBackgroundDrawable;
    }

    public View getContentViewForVm() {
        return mContentView;
    }

    public int getLayoutWidthForVm() {
        return mLayoutWidth;
    }

    public int getLayoutHeightForVm() {
        return mLayoutHeight;
    }

    public int getLayoutGravityForVm() {
        return mLayoutGravity;
    }

    public float getDimAmount() {
        return mDimAmount;
    }

    public void setDimAmount(float amount) {
        this.mDimAmount = amount;
    }

    public void setDecorFitsSystemWindows(boolean decorFitsSystemWindows) {
    }

    public boolean isDecorFitsSystemWindows() {
        return true;
    }

    public void setStatusBarContrastEnforced(boolean enforce) {
    }

    public void setNavigationBarDividerColor(int color) {
    }

    public void setNavigationBarContrastEnforced(boolean enforce) {
    }

    public void setSoftInputMode(int mode) {
        getAttributes().softInputMode = mode;
    }

    public void setGravity(int gravity) {
        this.mLayoutGravity = gravity;
    }

    public void setLayout(int width, int height) {
        this.mLayoutWidth = width;
        this.mLayoutHeight = height;
        WindowManager.LayoutParams attrs = getAttributes();
        attrs.width = width;
        attrs.height = height;
    }

    public void setWindowAnimations(int resId) {
        getAttributes().windowAnimations = resId;
    }

    public View findViewById(int id) {
        View decor = peekDecorView();
        return decor != null ? decor.findViewById(id) : null;
    }

    private WindowManager.LayoutParams mAttributes;

    public void setAttributes(WindowManager.LayoutParams a) {
        this.mAttributes = a;
    }

    public WindowManager.LayoutParams getAttributes() {
        // AOSP：窗口恒有 LayoutParams（attach 时创建）；首个取用懒建。
        if (this.mAttributes == null) {
            this.mAttributes = new WindowManager.LayoutParams();
        }
        return this.mAttributes;
    }

    public void takeSurface(SurfaceHolder.Callback2 callback) {
    }

    /** SurfaceHolder 最小面（AOSP android.view.SurfaceHolder 内嵌 Callback2）。 */
    public interface SurfaceHolder {
        interface Callback {
            void surfaceCreated(SurfaceHolder holder);

            void surfaceChanged(SurfaceHolder holder, int format, int width, int height);

            void surfaceDestroyed(SurfaceHolder holder);
        }

        interface Callback2 extends Callback {
            void surfaceRedrawNeeded(SurfaceHolder holder);
        }
    }

    /**
     * AOSP Window.OnFrameMetricsAvailableListener（Sentry FrameMetricsAggregator
     * 链接面；VM 无逐帧指标源，回调不会发生）。
     */
    public interface OnFrameMetricsAvailableListener {
        void onFrameMetricsAvailable(Window window, FrameMetrics frameMetrics,
                                     int dropCountSinceLastInvocation);
    }
}
