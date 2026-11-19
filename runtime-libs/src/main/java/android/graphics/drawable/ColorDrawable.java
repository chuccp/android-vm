package android.graphics.drawable;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.PorterDuff;

/**
 * 纯色可绘制（AOSP android.graphics.drawable.ColorDrawable 公开面；§31.7 C 档
 * lib——color/alpha 数据载体；像素由桥接渲染层落）。
 */
public class ColorDrawable extends Drawable {
    private int mColor = 0;

    public ColorDrawable() {
    }

    public ColorDrawable(int color) {
        this.mColor = color;
    }

    public int getColor() {
        return this.mColor;
    }

    public void setColor(int color) {
        this.mColor = color;
        invalidateSelf();
    }

    @Override
    public void draw(Canvas canvas) {
        // 像素由桥接渲染层落（VM 桥轨不消费 drawable 位图）
    }

    @Override
    public void setAlpha(int alpha) {
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
    }

    @Override
    public int getOpacity() {
        return 255;
    }

    @Override
    public ConstantState getConstantState() {
        return null;
    }
}
