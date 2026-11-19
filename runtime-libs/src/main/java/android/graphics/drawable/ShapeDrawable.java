package android.graphics.drawable;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Shader;
import android.graphics.drawable.shapes.Shape;

/**
 * android.graphics.drawable.ShapeDrawable（AOSP 公开面）。
 *
 * <p>Shape + Paint 的组合 Drawable；本 VM 的最终像素归宿主物化器，
 * 这里保留 shape/paint/intrinsic 字段与 AOSP 公开方法。
 */
public class ShapeDrawable extends Drawable {

    public abstract static class ShaderFactory {
        public abstract Shader resize(int width, int height);
    }

    private Shape mShape;
    private final Paint mPaint = new Paint();
    private ShaderFactory mShaderFactory;
    private int mIntrinsicWidth;
    private int mIntrinsicHeight;

    public ShapeDrawable() {
    }

    public ShapeDrawable(Shape shape) {
        mShape = shape;
    }

    public Shape getShape() {
        return mShape;
    }

    public void setShape(Shape shape) {
        mShape = shape;
    }

    public Paint getPaint() {
        return mPaint;
    }

    public void setShaderFactory(ShaderFactory factory) {
        mShaderFactory = factory;
    }

    public ShaderFactory getShaderFactory() {
        return mShaderFactory;
    }

    @Override
    public void draw(Canvas canvas) {
        if (mShape != null) {
            mShape.draw(canvas, mPaint);
        }
    }

    @Override
    public void setAlpha(int alpha) {
        mPaint.setAlpha(alpha);
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        mPaint.setColorFilter(colorFilter);
    }

    @Override
    public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
    }

    public void setIntrinsicWidth(int width) {
        mIntrinsicWidth = width;
    }

    public void setIntrinsicHeight(int height) {
        mIntrinsicHeight = height;
    }

    @Override
    public int getIntrinsicWidth() {
        return mIntrinsicWidth;
    }

    @Override
    public int getIntrinsicHeight() {
        return mIntrinsicHeight;
    }

    private final ConstantState mState = new ConstantState() {
        @Override
        public Drawable newDrawable() {
            return new ShapeDrawable();
        }
    };

    @Override
    public ConstantState getConstantState() {
        return mState;
    }
}
