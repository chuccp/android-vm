package android.view;

import android.content.Context;

/**
 * 视图配置（AOSP android.view.ViewConfiguration 公开面；§31.7 C 档 lib——触摸/
 * 滚动常量面）。
 */
public class ViewConfiguration {

    private static final int EDGE_SLOP = 12;
    private static final int TOUCH_SLOP = 8;
    private static final int FADING_EDGE_LENGTH = 12;
    private static final int MINIMUM_FLING_VELOCITY = 50;
    private static final int MAXIMUM_FLING_VELOCITY = 8000;
    private static final int SCROLL_BAR_SIZE = 10;
    private static final int LONG_PRESS_TIMEOUT = 500;
    private static final int TAP_TIMEOUT = 100;
    private static final int DOUBLE_TAP_TIMEOUT = 300;
    private static final int JUMP_TAP_TIMEOUT = 500;

    private static final ViewConfiguration sInstance = new ViewConfiguration();

    protected ViewConfiguration() {
    }

    @Deprecated
    public static ViewConfiguration get(Context context) {
        return sInstance;
    }

    public static int getScrollBarSize() {
        return SCROLL_BAR_SIZE;
    }

    public static int getScrollDefaultDelay() {
        return 300;
    }

    public static int getScrollFriction() {
        return 1;
    }

    public static int getEdgeSlop() {
        return EDGE_SLOP;
    }

    public static int getTouchSlop() {
        return TOUCH_SLOP;
    }

    public static int getFadingEdgeLength() {
        return FADING_EDGE_LENGTH;
    }

    public static int getMinimumFlingVelocity() {
        return MINIMUM_FLING_VELOCITY;
    }

    public static int getMaximumFlingVelocity() {
        return MAXIMUM_FLING_VELOCITY;
    }

    public static int getLongPressTimeout() {
        return LONG_PRESS_TIMEOUT;
    }

    public static int getTapTimeout() {
        return TAP_TIMEOUT;
    }

    public static int getJumpTapTimeout() {
        return JUMP_TAP_TIMEOUT;
    }

    public static int getDoubleTapTimeout() {
        return DOUBLE_TAP_TIMEOUT;
    }

    public static int getKeyRepeatTimeout() {
        return 500;
    }

    public static int getKeyRepeatDelay() {
        return 50;
    }

    public int getScaledScrollBarSize() {
        return SCROLL_BAR_SIZE;
    }

    public int getScaledTouchSlop() {
        return TOUCH_SLOP;
    }

    public int getScaledPagingTouchSlop() {
        return TOUCH_SLOP * 2;
    }

    public int getScaledEdgeSlop() {
        return EDGE_SLOP;
    }

    public int getScaledFadingEdgeLength() {
        return FADING_EDGE_LENGTH;
    }

    public int getScaledMinimumFlingVelocity() {
        return MINIMUM_FLING_VELOCITY;
    }

    public int getScaledMaximumFlingVelocity() {
        return MAXIMUM_FLING_VELOCITY;
    }

    public int getScaledOverflingDistance() {
        return FADING_EDGE_LENGTH;
    }

    public int getScaledOverscrollDistance() {
        return FADING_EDGE_LENGTH;
    }

    public boolean hasPermanentMenuKey() {
        return false;
    }

    public boolean shouldShowMenuShortcutsWhenKeyboardPresent() {
        return false;
    }

    public int getScaledMinimumTouchTargetSize() {
        return 48;
    }

    // API 26：ViewPager2 等构造期读取（缺失即 NoSuchMethodError → 构造中止）。
    public float getScaledHorizontalScrollFactor() {
        return 0.02f;
    }

    public float getScaledVerticalScrollFactor() {
        return 0.02f;
    }
}
