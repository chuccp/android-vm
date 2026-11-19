package android.graphics.drawable;

import android.content.res.ColorStateList;

/**
 * android.graphics.drawable.RippleDrawable（AOSP 公开面骨架）。
 *
 * <p>涟漪动画归宿主渲染面；本面承载 content/mask 结构（继承 LayerDrawable，
 * draw/setBounds 语义沿用），setColor/setRadius 为 no-op 记录。
 */
public class RippleDrawable extends LayerDrawable {
    public static final int RADIUS_AUTO = -1;

    private ColorStateList mColor;
    private int mRadius = RADIUS_AUTO;

    public RippleDrawable(ColorStateList color, Drawable content, Drawable mask) {
        super(content != null ? new Drawable[] {content} : new Drawable[0]);
        mColor = color;
    }

    public void setColor(ColorStateList color) {
        mColor = color;
    }

    public ColorStateList getColor() {
        return mColor;
    }

    public void setRadius(int radius) {
        mRadius = radius;
    }

    public int getRadius() {
        return mRadius;
    }

    public void setPadding(int left, int top, int right, int bottom) {
        // 宿主面无 ripple padding 语义；保留签名。
    }

    @Override
    public boolean isStateful() {
        return true;
    }
}
