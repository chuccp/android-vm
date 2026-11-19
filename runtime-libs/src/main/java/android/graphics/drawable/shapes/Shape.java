package android.graphics.drawable.shapes;

import android.graphics.Canvas;
import android.graphics.Paint;

/**
 * android.graphics.drawable.shapes.Shape（AOSP 公开面）。
 *
 * <p>ShapeDrawable 的几何载体；本 VM 的绘制归宿主物化器，shape 只保留
 * 尺寸/绘制接口（画布命令空转）。
 */
public abstract class Shape implements Cloneable {

    private float mWidth;
    private float mHeight;

    public final float getWidth() {
        return mWidth;
    }

    public final float getHeight() {
        return mHeight;
    }

    public abstract void draw(Canvas canvas, Paint paint);

    public void resize(float width, float height) {
        mWidth = width;
        mHeight = height;
    }

    public boolean hasAlpha() {
        return false;
    }

    @Override
    public Shape clone() throws CloneNotSupportedException {
        return (Shape) super.clone();
    }
}
