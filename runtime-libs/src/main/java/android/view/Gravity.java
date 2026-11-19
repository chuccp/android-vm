package android.view;

import android.graphics.Rect;

/**
 * 重力常量与工具（AOSP android.view.Gravity 公开面；§31.7 C 档 lib——纯常量/纯
 * 逻辑，无 VM 布局需求，一律 lib Java 源）。
 */
public class Gravity {
    public static final int NO_GRAVITY = 0x0000;
    public static final int TOP = 0x30;
    public static final int BOTTOM = 0x50;
    public static final int LEFT = 0x03;
    public static final int RIGHT = 0x05;
    public static final int START = 0x00800003;
    public static final int END = 0x00800005;
    public static final int CENTER_VERTICAL = 0x10;
    public static final int CENTER_HORIZONTAL = 0x01;
    public static final int CENTER = 0x11;
    public static final int FILL_VERTICAL = 0x70;
    public static final int FILL_HORIZONTAL = 0x07;
    public static final int FILL = 0x77;
    public static final int RELATIVE_LAYOUT_DIRECTION = 0x00800000;
    public static final int DISPLAY_CLIP_VERTICAL = 0x10000000;
    public static final int DISPLAY_CLIP_HORIZONTAL = 0x01000000;
    public static final int AXIS_SPECIFIED = 0x0001;
    public static final int AXIS_PULL_BEFORE = 0x0002;
    public static final int AXIS_PULL_AFTER = 0x0004;
    public static final int AXIS_X_SHIFT = 0;
    public static final int AXIS_Y_SHIFT = 4;

    public Gravity() {
    }

    public static int getAbsoluteGravity(int gravity, int layoutDirection) {
        int result = gravity;
        if ((result & RELATIVE_LAYOUT_DIRECTION) != 0) {
            if (layoutDirection == View.LAYOUT_DIRECTION_RTL) {
                // START/END 反转
                if ((result & LEFT) == LEFT) {
                    result = (result & ~LEFT) | RIGHT;
                } else if ((result & RIGHT) == RIGHT) {
                    result = (result & ~RIGHT) | LEFT;
                }
            }
            result &= ~RELATIVE_LAYOUT_DIRECTION;
        }
        return result;
    }

    public static void apply(int gravity, int w, int h, Rect container,
                             Rect outRect, int layoutDirection) {
        int abs = getAbsoluteGravity(gravity, layoutDirection);
        apply(abs, w, h, container, outRect);
    }

    public static void apply(int gravity, int w, int h, Rect container, int xAdj, int yAdj,
                             Rect outRect) {
        apply(gravity & ~RELATIVE_LAYOUT_DIRECTION, w, h, container, xAdj, yAdj, outRect);
    }

    public static void apply(int gravity, int w, int h, Rect container, int xAdj, int yAdj,
                             Rect outRect, int layoutDirection) {
        int abs = getAbsoluteGravity(gravity, layoutDirection);
        apply(abs, w, h, container, xAdj, yAdj, outRect);
    }

    public static void apply(int gravity, int w, int h, Rect container, Rect outRect) {
        int horiz = gravity & (LEFT | RIGHT | CENTER_HORIZONTAL);
        int vert = gravity & (TOP | BOTTOM | CENTER_VERTICAL);
        int l = container.left;
        int t = container.top;
        int x;
        if ((horiz & CENTER_HORIZONTAL) != 0) {
            x = l + ((container.width() - w) / 2);
        } else if ((horiz & RIGHT) != 0) {
            x = container.right - w;
        } else {
            x = l;
        }
        int y;
        if ((vert & CENTER_VERTICAL) != 0) {
            y = t + ((container.height() - h) / 2);
        } else if ((vert & BOTTOM) != 0) {
            y = container.bottom - h;
        } else {
            y = t;
        }
        outRect.set(x, y, x + w, y + h);
    }

    public static void applyDisplay(int gravity, Rect display, Rect inoutObj) {
        // 越界回收（AOSP applyDisplay 最小面）
        if ((gravity & TOP) != 0) {
            if (inoutObj.top < display.top) inoutObj.top = display.top;
        } else if ((gravity & BOTTOM) != 0) {
            if (inoutObj.bottom > display.bottom) inoutObj.bottom = display.bottom;
        }
        if ((gravity & LEFT) != 0) {
            if (inoutObj.left < display.left) inoutObj.left = display.left;
        } else if ((gravity & RIGHT) != 0) {
            if (inoutObj.right > display.right) inoutObj.right = display.right;
        }
    }

    public static boolean isHorizontal(int gravity) {
        return (gravity & (LEFT | RIGHT | CENTER_HORIZONTAL)) != 0;
    }

    public static boolean isVertical(int gravity) {
        return (gravity & (TOP | BOTTOM | CENTER_VERTICAL)) != 0;
    }
}
