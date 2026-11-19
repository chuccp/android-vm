package android.graphics;

/**
 * 像素格式常量（AOSP android.graphics.PixelFormat；§31.7 C 档 lib 常量面）。
 */
public class PixelFormat {
    public static final int UNKNOWN = 0;
    public static final int TRANSLUCENT = -3;
    public static final int TRANSPARENT = -2;
    public static final int OPAQUE = -1;
    public static final int RGBA_8888 = 1;
    public static final int RGBX_8888 = 2;
    public static final int RGB_888 = 3;
    public static final int RGB_565 = 4;
    public static final int RGBA_5551 = 6;
    public static final int RGBA_4444 = 7;
    public static final int A_8 = 8;
    public static final int L_8 = 9;
    public static final int LA_88 = 10;
    public static final int RGB_332 = 11;

    public static boolean isFormatOpaque(int format) {
        return format == OPAQUE || format == RGBX_8888 || format == RGB_888
                || format == RGB_565 || format == L_8 || format == RGB_332;
    }
}
