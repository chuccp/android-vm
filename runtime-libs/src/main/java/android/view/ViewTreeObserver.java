package android.view;

import java.util.ArrayList;
import java.util.List;

/**
 * 视图树观察者（AOSP android.view.ViewTreeObserver 公开面；§31.7 C 档 lib——
 * 监听器登记面；全局绘制/布局事件归桥接层）。
 */
public class ViewTreeObserver {

    public interface OnGlobalFocusChangeListener {
        void onGlobalFocusChanged(View oldFocus, View newFocus);
    }

    public interface OnGlobalLayoutListener {
        void onGlobalLayout();
    }

    public interface OnPreDrawListener {
        boolean onPreDraw();
    }

    public interface OnScrollChangedListener {
        void onScrollChanged();
    }

    public interface OnTouchModeChangeListener {
        void onTouchModeChanged(boolean isInTouchMode);
    }

    public interface OnDrawListener {
        void onDraw();
    }

    public interface OnWindowAttachListener {
        void onWindowAttached();

        void onWindowDetached();
    }

    public interface OnWindowFocusChangeListener {
        void onWindowFocusChanged(boolean hasWindowFocus);
    }

    private final List<OnGlobalLayoutListener> mOnGlobalLayoutListeners =
            new ArrayList<OnGlobalLayoutListener>();
    private final List<OnPreDrawListener> mOnPreDrawListeners =
            new ArrayList<OnPreDrawListener>();
    private final List<OnDrawListener> mOnDrawListeners = new ArrayList<OnDrawListener>();
    private final List<OnScrollChangedListener> mOnScrollChangedListeners =
            new ArrayList<OnScrollChangedListener>();
    private final List<OnWindowFocusChangeListener> mOnWindowFocusChangeListeners =
            new ArrayList<OnWindowFocusChangeListener>();
    private boolean mAlive = true;

    public ViewTreeObserver() {
    }

    public void merge(ViewTreeObserver observer) {
    }

    public boolean isAlive() {
        return this.mAlive;
    }

    public void addOnGlobalFocusChangeListener(OnGlobalFocusChangeListener listener) {
    }

    public void removeOnGlobalFocusChangeListener(OnGlobalFocusChangeListener listener) {
    }

    public void addOnGlobalLayoutListener(OnGlobalLayoutListener listener) {
        this.mOnGlobalLayoutListeners.add(listener);
    }

    @Deprecated
    public void removeGlobalOnLayoutListener(OnGlobalLayoutListener listener) {
        this.mOnGlobalLayoutListeners.remove(listener);
    }

    public void removeOnGlobalLayoutListener(OnGlobalLayoutListener listener) {
        this.mOnGlobalLayoutListeners.remove(listener);
    }

    public void addOnPreDrawListener(OnPreDrawListener listener) {
        this.mOnPreDrawListeners.add(listener);
    }

    public void removeOnPreDrawListener(OnPreDrawListener listener) {
        this.mOnPreDrawListeners.remove(listener);
    }

    public void addOnDrawListener(OnDrawListener listener) {
        this.mOnDrawListeners.add(listener);
    }

    public void removeOnDrawListener(OnDrawListener listener) {
        this.mOnDrawListeners.remove(listener);
    }

    public void addOnScrollChangedListener(OnScrollChangedListener listener) {
        this.mOnScrollChangedListeners.add(listener);
    }

    public void removeOnScrollChangedListener(OnScrollChangedListener listener) {
        this.mOnScrollChangedListeners.remove(listener);
    }

    public void addOnTouchModeChangeListener(OnTouchModeChangeListener listener) {
    }

    public void removeOnTouchModeChangeListener(OnTouchModeChangeListener listener) {
    }

    public void addOnWindowAttachListener(OnWindowAttachListener listener) {
    }

    public void removeOnWindowAttachListener(OnWindowAttachListener listener) {
    }

    public void addOnWindowFocusChangeListener(OnWindowFocusChangeListener listener) {
        this.mOnWindowFocusChangeListeners.add(listener);
    }

    public void removeOnWindowFocusChangeListener(OnWindowFocusChangeListener listener) {
        this.mOnWindowFocusChangeListeners.remove(listener);
    }

    public void dispatchOnGlobalLayout() {
        for (OnGlobalLayoutListener listener
                : new ArrayList<OnGlobalLayoutListener>(this.mOnGlobalLayoutListeners)) {
            listener.onGlobalLayout();
        }
    }

    public boolean dispatchOnPreDraw() {
        boolean cancelDraw = false;
        for (OnPreDrawListener listener
                : new ArrayList<OnPreDrawListener>(this.mOnPreDrawListeners)) {
            cancelDraw |= !listener.onPreDraw();
        }
        return !cancelDraw;
    }

    public void dispatchOnDraw() {
        for (OnDrawListener listener
                : new ArrayList<OnDrawListener>(this.mOnDrawListeners)) {
            listener.onDraw();
        }
    }

    public void dispatchOnScrollChanged() {
        for (OnScrollChangedListener listener
                : new ArrayList<OnScrollChangedListener>(this.mOnScrollChangedListeners)) {
            listener.onScrollChanged();
        }
    }

    public void dispatchOnWindowFocusChange(boolean hasWindowFocus) {
        for (OnWindowFocusChangeListener listener
                : new ArrayList<OnWindowFocusChangeListener>(this.mOnWindowFocusChangeListeners)) {
            listener.onWindowFocusChanged(hasWindowFocus);
        }
    }

    public void requestFitSystemWindows() {
    }
}
