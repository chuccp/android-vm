package android.graphics;

/**
 * 颜色空间（AOSP android.graphics.ColorSpace；§31.7 C 档 lib 骨架）。
 * 仅 Bitmap.getColorSpace 引用面载体。
 */
public class ColorSpace {

    public enum Named {
        SRGB,
        LINEAR_SRGB,
        EXTENDED_SRGB,
        BT709
    }

    private static final ColorSpace SRGB = new ColorSpace("sRGB");
    private static final ColorSpace LINEAR_SRGB = new ColorSpace("Linear sRGB");
    private static final ColorSpace EXTENDED_SRGB = new ColorSpace("Extended sRGB");
    private static final ColorSpace BT709 = new ColorSpace("BT.709");

    private final String mName;

    ColorSpace(String name) {
        this.mName = name;
    }

    public static ColorSpace get(Named name) {
        if (name == null) return SRGB;
        switch (name) {
            case LINEAR_SRGB:
                return LINEAR_SRGB;
            case EXTENDED_SRGB:
                return EXTENDED_SRGB;
            case BT709:
                return BT709;
            default:
                return SRGB;
        }
    }

    public String getName() {
        return this.mName;
    }

    public int getId() {
        return 0;
    }

    @Override
    public String toString() {
        return "ColorSpace(" + this.mName + ")";
    }
}
