package android.view;

import android.graphics.Rect;

/**
 * 窗口嵌入（AOSP android.view.WindowInsets 公开面；§31.7 C 档 lib POJO——嵌入
 * 矩形字段态）。
 */
public class WindowInsets {

    public static final int SIDE_LEFT = 1;
    public static final int SIDE_TOP = 2;
    public static final int SIDE_RIGHT = 4;
    public static final int SIDE_BOTTOM = 8;
    public static final int SIDE_ALL = 15;

    private final Rect mSystemWindowInsets = new Rect();
    private final Rect mStableInsets = new Rect();
    private final Rect mDisplayCutout = new Rect();
    private boolean mConsumed;

    public WindowInsets(WindowInsets source) {
        if (source != null) {
            this.mSystemWindowInsets.set(source.mSystemWindowInsets);
            this.mStableInsets.set(source.mStableInsets);
            this.mConsumed = source.mConsumed;
        }
    }

    public WindowInsets(Rect systemWindowInsets) {
        if (systemWindowInsets != null) this.mSystemWindowInsets.set(systemWindowInsets);
    }

    public int getSystemWindowInsetLeft() {
        return this.mSystemWindowInsets.left;
    }

    public int getSystemWindowInsetTop() {
        return this.mSystemWindowInsets.top;
    }

    public int getSystemWindowInsetRight() {
        return this.mSystemWindowInsets.right;
    }

    public int getSystemWindowInsetBottom() {
        return this.mSystemWindowInsets.bottom;
    }

    public boolean hasSystemWindowInsets() {
        return !this.mSystemWindowInsets.isEmpty();
    }

    public int getStableInsetLeft() {
        return this.mStableInsets.left;
    }

    public int getStableInsetTop() {
        return this.mStableInsets.top;
    }

    public int getStableInsetRight() {
        return this.mStableInsets.right;
    }

    public int getStableInsetBottom() {
        return this.mStableInsets.bottom;
    }

    public boolean hasStableInsets() {
        return !this.mStableInsets.isEmpty();
    }

    public boolean isConsumed() {
        return this.mConsumed;
    }

    public WindowInsets consumeSystemWindowInsets() {
        this.mSystemWindowInsets.setEmpty();
        return this;
    }

    public WindowInsets consumeStableInsets() {
        this.mStableInsets.setEmpty();
        return this;
    }

    public WindowInsets replaceSystemWindowInsets(int left, int top, int right, int bottom) {
        this.mSystemWindowInsets.set(left, top, right, bottom);
        return this;
    }

    public WindowInsets replaceSystemWindowInsets(Rect systemWindowInsets) {
        if (systemWindowInsets != null) this.mSystemWindowInsets.set(systemWindowInsets);
        return this;
    }

    public WindowInsets inset(Rect insets) {
        return this;
    }

    @Override
    public boolean equals(Object o) {
        return this == o;
    }

    @Override
    public int hashCode() {
        return System.identityHashCode(this);
    }

    @Override
    public String toString() {
        return "WindowInsets{" + this.mSystemWindowInsets.toShortString() + "}";
    }

    /** 构建器（AOSP WindowInsets.Builder）。 */
    public static final class Builder {
        private final Rect mInsets = new Rect();

        public Builder() {
        }

        public Builder(WindowInsets insets) {
            if (insets != null) this.mInsets.set(insets.mSystemWindowInsets);
        }

        public Builder setSystemWindowInsets(Rect insets) {
            if (insets != null) this.mInsets.set(insets);
            return this;
        }

        public Builder setStableInsets(Rect insets) {
            return this;
        }

        public WindowInsets build() {
            return new WindowInsets(this.mInsets);
        }
    }
}
