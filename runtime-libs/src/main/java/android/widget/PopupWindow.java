package android.widget;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.view.VmLayoutHost;
import android.view.View;
import android.view.ViewParent;

/**
 * 弹出窗口（AOSP android.widget.PopupWindow；§31.7 C 档 lib 骨架 +
 * VmLayoutHost 叠加窗口桥）。
 *
 * <p>内容树经 {@link VmLayoutHost#showPopupWindow} 登记为独立叠加窗口根
 * （无 dim、按锚点坐标定位），宿主物化浮层卡片（elevation 投影）；宿主外点
 * 关闭经 {@code onHostDismissed()} 回灌并派发 OnDismissListener。
 */
public class PopupWindow {

    public static final int INPUT_METHOD_FROM_FOCUSABLE = 0;
    public static final int INPUT_METHOD_NEEDED = 1;
    public static final int INPUT_METHOD_NOT_NEEDED = 2;

    public interface OnDismissListener {
        void onDismiss();
    }

    private View mContentView;
    private boolean mShowing;
    private int mWidth = -2;
    private int mHeight = -2;
    private OnDismissListener mOnDismissListener;

    public PopupWindow(Context context) {
    }

    public PopupWindow(View contentView) {
        this.mContentView = contentView;
    }

    public PopupWindow(View contentView, int width, int height) {
        this.mContentView = contentView;
        this.mWidth = width;
        this.mHeight = height;
    }

    public PopupWindow(View contentView, int width, int height, boolean focusable) {
        this(contentView, width, height);
    }

    public Drawable getBackground() {
        return null;
    }

    public void setBackgroundDrawable(Drawable background) {
    }

    public void setFocusable(boolean focusable) {
    }

    public boolean isFocusable() {
        return false;
    }

    public void setOutsideTouchable(boolean touchable) {
    }

    public boolean isOutsideTouchable() {
        return false;
    }

    public void setIgnoreCheekPress() {
    }

    public void setAnimationStyle(int animationStyle) {
    }

    public int getAnimationStyle() {
        return 0;
    }

    public View getContentView() {
        return this.mContentView;
    }

    public void setContentView(View contentView) {
        this.mContentView = contentView;
    }

    public void setOnDismissListener(OnDismissListener listener) {
        this.mOnDismissListener = listener;
    }

    public void setWindowLayoutType(int layoutType) {
    }

    public void setWindowLayoutMode(int widthSpec, int heightSpec) {
    }

    /** 锚点视图在 guest 逻辑窗口内的绝对坐标（沿父链累加 + 扣滚动偏移）。 */
    private static int[] absolutePos(View anchor) {
        int x = 0;
        int y = 0;
        View cur = anchor;
        for (int depth = 0; cur != null && depth < 64; depth++) {
            x += cur.getLeft() - cur.getScrollX();
            y += cur.getTop() - cur.getScrollY();
            ViewParent parent = cur.getParent();
            cur = parent instanceof View ? (View) parent : null;
        }
        return new int[] {x, y};
    }

    private void showInternal(int x, int y) {
        if (mContentView == null || mShowing) {
            return;
        }
        mShowing = true;
        VmLayoutHost.showPopupWindow(this, mContentView, x, y, mWidth, mHeight);
    }

    public void showAsDropDown(View anchor) {
        showAsDropDown(anchor, 0, 0);
    }

    public void showAsDropDown(View anchor, int xoff, int yoff) {
        if (anchor == null) {
            return;
        }
        int[] pos = absolutePos(anchor);
        showInternal(pos[0] + xoff, pos[1] + (anchor.getBottom() - anchor.getTop()) + yoff);
    }

    public void showAsDropDown(View anchor, int xoff, int yoff, int gravity) {
        showAsDropDown(anchor, xoff, yoff);
    }

    public void showAtLocation(View parent, int gravity, int x, int y) {
        // guest 逻辑窗口坐标系；gravity 由宿主浮层定位策略吸收。
        showInternal(x, y);
    }

    public boolean isShowing() {
        return this.mShowing;
    }

    public void dismiss() {
        if (!mShowing) {
            return;
        }
        this.mShowing = false;
        VmLayoutHost.dismissPopupWindow(this);
        if (mOnDismissListener != null) {
            mOnDismissListener.onDismiss();
        }
    }

    /** 宿主外点关闭回灌：同步展示态并派发 OnDismissListener。 */
    public void onHostDismissed() {
        if (!mShowing) {
            return;
        }
        this.mShowing = false;
        if (mOnDismissListener != null) {
            mOnDismissListener.onDismiss();
        }
    }

    public void setFocusableMode(int mode) {
    }

    public void update(View anchor, int xoff, int yoff, int width, int height) {
        this.mWidth = width;
        this.mHeight = height;
    }

    public void update(int width, int height) {
        this.mWidth = width;
        this.mHeight = height;
    }

    public void update(int x, int y, int width, int height) {
    }

    public void update(int x, int y, int width, int height, boolean force) {
    }

    public int getWidth() {
        return this.mWidth;
    }

    public void setWidth(int width) {
        this.mWidth = width;
    }

    public int getHeight() {
        return this.mHeight;
    }

    public void setHeight(int height) {
        this.mHeight = height;
    }
}
