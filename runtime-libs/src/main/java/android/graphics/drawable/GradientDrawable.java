package android.graphics.drawable;

import android.graphics.ColorFilter;

/**
 * 渐变可绘制（AOSP android.graphics.drawable.GradientDrawable 公开面；
 * §31.7 C 档 lib 骨架——渐变态；光栅化归桥接渲染层）。
 */
public class GradientDrawable extends Drawable {

    /** 渐变方向（AOSP GradientDrawable.Orientation 枚举）。 */
    public enum Orientation {
        TOP_BOTTOM,
        TR_BL,
        RIGHT_LEFT,
        BR_TL,
        BOTTOM_TOP,
        BL_TR,
        LEFT_RIGHT,
        TL_BR
    }

    public static final int RECTANGLE = 0;
    public static final int OVAL = 1;
    public static final int LINE = 2;
    public static final int RING = 3;

    private int mShape = RECTANGLE;
    private int mGradientType;
    private Orientation mOrientation = Orientation.TOP_BOTTOM;
    private int mColor;
    private float mCornerRadius;

    public GradientDrawable() {
    }

    public GradientDrawable(Orientation orientation, int[] colors) {
        this.mOrientation = orientation;
    }

    public void setColor(int argb) {
        this.mColor = argb;
    }

    public void setColors(int[] colors) {
    }

    public void setOrientation(Orientation orientation) {
        this.mOrientation = orientation != null ? orientation : Orientation.TOP_BOTTOM;
    }

    public void setGradientType(int gradient) {
        this.mGradientType = gradient;
    }

    public void setShape(int shape) {
        this.mShape = shape;
    }

    public void setCornerRadius(float radius) {
        this.mCornerRadius = radius;
    }

    public void setCornerRadii(float[] radii) {
    }

    public void setStroke(int width, int color) {
    }

    public void setStroke(int width, int color, float dashWidth, float dashGap) {
    }

    public void setSize(int width, int height) {
    }

    public void setGradientCenter(float x, float y) {
    }

    public void setGradientRadius(float gradientRadius) {
    }

    @Override
    public void draw(android.graphics.Canvas canvas) {
    }

    @Override
    public void setAlpha(int alpha) {
    }

    @Override
    public void setColorFilter(ColorFilter cf) {
    }

    @Override
    public ConstantState getConstantState() {
        return null;
    }
}
