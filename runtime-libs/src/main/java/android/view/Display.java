package android.view;

/**
 * 显示器（AOSP android.view.Display；§31.7 C 档 lib 骨架——度量面）。
 */
public class Display {

    private final int mWidth;
    private final int mHeight;

    public Display(int width, int height) {
        this.mWidth = width;
        this.mHeight = height;
    }

    public int getWidth() {
        return this.mWidth;
    }

    public int getHeight() {
        return this.mHeight;
    }

    public void getMetrics(android.util.DisplayMetrics outMetrics) {
        if (outMetrics != null) {
            outMetrics.widthPixels = this.mWidth;
            outMetrics.heightPixels = this.mHeight;
        }
    }

    public void getRealMetrics(android.util.DisplayMetrics outMetrics) {
        getMetrics(outMetrics);
    }

    public int getRotation() {
        return 0;
    }

    public float getRefreshRate() {
        return 60.0f;
    }

    public int getDisplayId() {
        return 0;
    }

    public String getName() {
        return "display0";
    }

    @Override
    public String toString() {
        return "Display(" + this.mWidth + "x" + this.mHeight + ")";
    }
}
