package android.widget;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.view.View;

/**
 * 弹出窗口（AOSP android.widget.PopupWindow；§31.7 C 档 lib 骨架——弹出态字段 +
 * show/dismiss 空转；真窗口归桥接层）。
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
    }

    public void setWindowLayoutType(int layoutType) {
    }

    public void setWindowLayoutMode(int widthSpec, int heightSpec) {
    }

    public void showAsDropDown(View anchor) {
    }

    public void showAsDropDown(View anchor, int xoff, int yoff) {
    }

    public void showAsDropDown(View anchor, int xoff, int yoff, int gravity) {
    }

    public void showAtLocation(View parent, int gravity, int x, int y) {
        this.mShowing = true;
    }

    public boolean isShowing() {
        return this.mShowing;
    }

    public void dismiss() {
        this.mShowing = false;
    }

    public void setFocusableMode(int mode) {
    }

    public void update(View anchor, int xoff, int yoff, int width, int height) {
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
