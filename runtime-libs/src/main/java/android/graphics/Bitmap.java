package android.graphics;

import android.hardware.HardwareBuffer;

import java.io.OutputStream;

/**
 * 位图（AOSP android.graphics.Bitmap 公开面；§31.7 C 档 lib——「lib Java + native
 * 缝」形态：像素缓冲存 Java int[]（ARGB_8888），Canvas.drawBitmap 经字段缝读取）。
 *
 * <p>布局契约：mBuffer = width×height ARGB int（行优先）；native 渲染缝
 * （framework/render.cc）经 FindInstanceField("mBuffer"/"mWidth"/"mHeight") 消费。
 * 编解码（BitmapFactory 真解码）后置。
 */
public final class Bitmap {
    public enum Config {
        ALPHA_8,
        RGB_565,
        ARGB_4444,
        ARGB_8888,
        RGBA_F16,
        HARDWARE
    }

    public enum CompressFormat {
        JPEG,
        PNG,
        WEBP
    }

    int mWidth;
    int mHeight;
    Config mConfig = Config.ARGB_8888;
    int[] mBuffer;
    boolean mRecycled;
    boolean mHasAlpha = true;
    int mDensity = 160;

    Bitmap(int width, int height, Config config) {
        this.mWidth = Math.max(0, width);
        this.mHeight = Math.max(0, height);
        this.mConfig = config != null ? config : Config.ARGB_8888;
        this.mBuffer = new int[this.mWidth * this.mHeight];
    }

    public static Bitmap createBitmap(int width, int height, Config config) {
        return new Bitmap(width, height, config);
    }

    public static Bitmap createBitmap(int width, int height, Config config, boolean hasAlpha) {
        Bitmap b = new Bitmap(width, height, config);
        b.mHasAlpha = hasAlpha;
        return b;
    }

    public static Bitmap createBitmap(Bitmap source) {
        return createBitmap(source, 0, 0, source != null ? source.mWidth : 0,
                source != null ? source.mHeight : 0);
    }

    public static Bitmap createBitmap(Picture source) {
        return new Bitmap(1, 1, Config.ARGB_8888);
    }

    public static Bitmap createBitmap(Bitmap source, int x, int y, int width, int height) {
        if (source == null) throw new NullPointerException("source == null");
        Bitmap b = new Bitmap(width, height, source.mConfig);
        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                int sx = x + col;
                int sy = y + row;
                if (sx >= 0 && sx < source.mWidth && sy >= 0 && sy < source.mHeight
                        && !source.mRecycled) {
                    b.mBuffer[row * width + col] = source.mBuffer[sy * source.mWidth + sx];
                }
            }
        }
        return b;
    }

    public static Bitmap createBitmap(Bitmap source, int x, int y, int width, int height,
                                      Matrix m, boolean filter) {
        return createBitmap(source, x, y, width, height);
    }

    public static Bitmap createBitmap(int[] colors, int width, int height, Config config) {
        Bitmap b = new Bitmap(width, height, config);
        if (colors != null) {
            int n = Math.min(colors.length, b.mBuffer.length);
            System.arraycopy(colors, 0, b.mBuffer, 0, n);
        }
        return b;
    }

    public static Bitmap createScaledBitmap(Bitmap src, int dstWidth, int dstHeight,
                                            boolean filter) {
        if (src == null) throw new NullPointerException("src == null");
        Bitmap b = new Bitmap(dstWidth, dstHeight, src.mConfig);
        for (int y = 0; y < dstHeight; y++) {
            int sy = src.mHeight == 0 ? 0 : y * src.mHeight / dstHeight;
            for (int x = 0; x < dstWidth; x++) {
                int sx = src.mWidth == 0 ? 0 : x * src.mWidth / dstWidth;
                if (!src.mRecycled && sy < src.mHeight && sx < src.mWidth) {
                    b.mBuffer[y * dstWidth + x] = src.mBuffer[sy * src.mWidth + sx];
                }
            }
        }
        return b;
    }

    public int getWidth() {
        return this.mWidth;
    }

    public int getHeight() {
        return this.mHeight;
    }

    public Config getConfig() {
        return this.mConfig;
    }

    public int getRowBytes() {
        return this.mWidth * 4;
    }

    public int getByteCount() {
        return this.mBuffer != null ? this.mBuffer.length * 4 : 0;
    }

    public int getAllocationByteCount() {
        return getByteCount();
    }

    public void setDensity(int density) {
        this.mDensity = density;
    }

    public int getDensity() {
        return this.mDensity;
    }

    public int getScaledWidth(int targetDensity) {
        return this.mWidth;
    }

    public int getScaledHeight(int targetDensity) {
        return this.mHeight;
    }

    public int getPixel(int x, int y) {
        checkRecycled();
        if (x < 0 || y < 0 || x >= this.mWidth || y >= this.mHeight) {
            throw new IllegalArgumentException("x/y out of bounds");
        }
        return this.mBuffer[y * this.mWidth + x];
    }

    public void setPixel(int x, int y, int color) {
        checkRecycled();
        if (x < 0 || y < 0 || x >= this.mWidth || y >= this.mHeight) {
            throw new IllegalArgumentException("x/y out of bounds");
        }
        this.mBuffer[y * this.mWidth + x] = color;
    }

    public void getPixels(int[] pixels, int offset, int stride, int x, int y,
                          int width, int height) {
        checkRecycled();
        if (pixels == null) throw new NullPointerException("pixels == null");
        for (int row = 0; row < height; row++) {
            int srcBase = (y + row) * this.mWidth + x;
            int dstBase = offset + row * stride;
            for (int col = 0; col < width; col++) {
                pixels[dstBase + col] = this.mBuffer[srcBase + col];
            }
        }
    }

    public void setPixels(int[] pixels, int offset, int stride, int x, int y,
                          int width, int height) {
        checkRecycled();
        if (pixels == null) throw new NullPointerException("pixels == null");
        for (int row = 0; row < height; row++) {
            int dstBase = (y + row) * this.mWidth + x;
            int srcBase = offset + row * stride;
            for (int col = 0; col < width; col++) {
                this.mBuffer[dstBase + col] = pixels[srcBase + col];
            }
        }
    }

    public void eraseColor(int color) {
        checkRecycled();
        for (int i = 0; i < this.mBuffer.length; i++) {
            this.mBuffer[i] = color;
        }
    }

    public Bitmap copy(Config config, boolean isMutable) {
        checkRecycled();
        Bitmap b = new Bitmap(this.mWidth, this.mHeight, config != null ? config : this.mConfig);
        b.mHasAlpha = this.mHasAlpha;
        System.arraycopy(this.mBuffer, 0, b.mBuffer, 0, this.mBuffer.length);
        return b;
    }

    public boolean compress(CompressFormat format, int quality, OutputStream stream) {
        checkRecycled();
        // 编码缝后置：语义骨架不产码流。
        return false;
    }

    public Bitmap extractAlpha() {
        checkRecycled();
        return copy(this.mConfig, true);
    }

    public void copyPixelsFromBuffer(java.nio.Buffer src) {
        checkRecycled();
    }

    public void copyPixelsToBuffer(java.nio.Buffer dst) {
        checkRecycled();
    }

    public boolean hasAlpha() {
        return this.mHasAlpha;
    }

    public void setHasAlpha(boolean hasAlpha) {
        this.mHasAlpha = hasAlpha;
    }

    public void recycle() {
        this.mRecycled = true;
        this.mBuffer = null;
    }

    public boolean isRecycled() {
        return this.mRecycled;
    }

    public ColorSpace getColorSpace() {
        return ColorSpace.get(ColorSpace.Named.SRGB);
    }

    public HardwareBuffer getHardwareBuffer() {
        return null;
    }

    public byte[] getNinePatchChunk() {
        return null;
    }

    public boolean isNinePatchChunk() {
        return false;
    }

    public void setPremultiplied(boolean premultiplied) {
    }

    private void checkRecycled() {
        if (this.mRecycled) {
            throw new IllegalStateException("Bitmap is recycled");
        }
    }

    @Override
    public String toString() {
        return "Bitmap(" + this.mWidth + "x" + this.mHeight + " " + this.mConfig + ")";
    }
}
