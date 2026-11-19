package android.graphics.drawable;

import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Paint;
import android.graphics.PixelFormat;

/**
 * 位图可绘制（AOSP android.graphics.drawable.BitmapDrawable 公开面；§31.7 C 档 lib
 * 骨架——位图态；光栅化归桥接渲染层）。
 */
public class BitmapDrawable extends Drawable {

    private Bitmap mBitmap;
    private Paint mPaint = new Paint();
    private int mTargetDensity = 160;

    public BitmapDrawable() {
    }

    @Deprecated
    public BitmapDrawable(Bitmap bitmap) {
        this.mBitmap = bitmap;
    }

    public BitmapDrawable(Resources res, Bitmap bitmap) {
        this.mBitmap = bitmap;
    }

    public BitmapDrawable(Resources res, String filepath) {
    }

    public BitmapDrawable(String filepath) {
    }

    public BitmapDrawable(java.io.InputStream is) {
    }

    public BitmapDrawable(Resources res, java.io.InputStream is) {
    }

    public final Paint getPaint() {
        return this.mPaint;
    }

    public final Bitmap getBitmap() {
        return this.mBitmap;
    }

    public void setTargetDensity(int density) {
        this.mTargetDensity = density;
    }

    public void setTargetDensity(android.util.DisplayMetrics metrics) {
    }

    public int getTargetDensity() {
        return this.mTargetDensity;
    }

    public int getIntrinsicWidth() {
        return this.mBitmap != null ? this.mBitmap.getWidth() : -1;
    }

    public int getIntrinsicHeight() {
        return this.mBitmap != null ? this.mBitmap.getHeight() : -1;
    }

    public void setAntiAlias(boolean aa) {
        this.mPaint.setAntiAlias(aa);
    }

    public void setFilterBitmap(boolean filter) {
        this.mPaint.setFilterBitmap(filter);
    }

    public void setDither(boolean dither) {
        this.mPaint.setDither(dither);
    }

    @Override
    public void draw(android.graphics.Canvas canvas) {
    }

    @Override
    public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
    }

    @Override
    public void setAlpha(int alpha) {
        this.mPaint.setAlpha(alpha);
    }

    @Override
    public void setColorFilter(android.graphics.ColorFilter cf) {
        this.mPaint.setColorFilter(cf);
    }

    @Override
    public ConstantState getConstantState() {
        return null;
    }
}
