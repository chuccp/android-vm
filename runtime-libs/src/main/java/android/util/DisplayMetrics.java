package android.util;

/**
 * 显示度量（AOSP android.util.DisplayMetrics 公开字段态；§31.7 C 档 lib POJO）。
 */
public class DisplayMetrics {
    public static final int DENSITY_DEFAULT = 160;

    public int widthPixels;
    public int heightPixels;
    public float density;
    public int densityDpi = DENSITY_DEFAULT;
    public float scaledDensity;
    public float xdpi = DENSITY_DEFAULT;
    public float ydpi = DENSITY_DEFAULT;

    public DisplayMetrics() {
        this.density = 1.0f;
        this.scaledDensity = 1.0f;
    }

    public void setTo(DisplayMetrics other) {
        if (other == null) return;
        this.widthPixels = other.widthPixels;
        this.heightPixels = other.heightPixels;
        this.density = other.density;
        this.densityDpi = other.densityDpi;
        this.scaledDensity = other.scaledDensity;
        this.xdpi = other.xdpi;
        this.ydpi = other.ydpi;
    }

    public void setToDefaults() {
        this.widthPixels = 0;
        this.heightPixels = 0;
        this.density = 1.0f;
        this.densityDpi = DENSITY_DEFAULT;
        this.scaledDensity = 1.0f;
        this.xdpi = DENSITY_DEFAULT;
        this.ydpi = DENSITY_DEFAULT;
    }

    @Override
    public String toString() {
        return "DisplayMetrics{density=" + this.density + ", width=" + this.widthPixels
                + ", height=" + this.heightPixels + ", scaledDensity=" + this.scaledDensity
                + ", xdpi=" + this.xdpi + ", ydpi=" + this.ydpi
                + ", densityDpi=" + this.densityDpi + "}";
    }
}
