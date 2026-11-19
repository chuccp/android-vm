package android.util;

/**
 * 类型化资源值（AOSP android.util.TypedValue 公开面；§31.7 C 档 lib POJO）。
 */
public class TypedValue {
    public static final int TYPE_NULL = 0x00;
    public static final int TYPE_REFERENCE = 0x01;
    public static final int TYPE_ATTRIBUTE = 0x02;
    public static final int TYPE_STRING = 0x03;
    public static final int TYPE_FLOAT = 0x04;
    public static final int TYPE_DIMENSION = 0x05;
    public static final int TYPE_FRACTION = 0x06;
    public static final int TYPE_INT_DEC = 0x10;
    public static final int TYPE_INT_HEX = 0x11;
    public static final int TYPE_INT_BOOLEAN = 0x12;

    public static final int COMPLEX_UNIT_SHIFT = 0;
    public static final int COMPLEX_UNIT_MASK = 0xf;
    public static final int COMPLEX_UNIT_PX = 0;
    public static final int COMPLEX_UNIT_DIP = 1;
    public static final int COMPLEX_UNIT_SP = 2;
    public static final int COMPLEX_UNIT_PT = 3;
    public static final int COMPLEX_UNIT_IN = 4;
    public static final int COMPLEX_UNIT_MM = 5;
    public static final int COMPLEX_UNIT_FRACTION = 0;
    public static final int COMPLEX_UNIT_FRACTION_PARENT = 1;
    public static final int COMPLEX_RADIX_SHIFT = 4;
    public static final int COMPLEX_RADIX_MASK = 0x3;
    public static final int COMPLEX_RADIX_23p0 = 0;
    public static final int COMPLEX_RADIX_16p7 = 1;
    public static final int COMPLEX_RADIX_8p15 = 2;
    public static final int COMPLEX_RADIX_0p23 = 3;
    public static final int COMPLEX_MANTISSA_SHIFT = 8;
    public static final int COMPLEX_MANTISSA_MASK = 0xffffff;

    public int type;
    public CharSequence string;
    public int data;
    public int assetCookie;
    public int resourceId;
    public int density;

    public TypedValue() {
    }

    public float getFloat() {
        return Float.intBitsToFloat(this.data);
    }

    /** 维度折算（AOSP TypedValue.applyDimension 同形）。 */
    public static float applyDimension(int unit, float value, DisplayMetrics metrics) {
        if (metrics == null) return value;
        switch (unit) {
            case COMPLEX_UNIT_PX:
                return value;
            case COMPLEX_UNIT_DIP:
                return value * metrics.density;
            case COMPLEX_UNIT_SP:
                return value * metrics.scaledDensity;
            case COMPLEX_UNIT_PT:
                return value * metrics.xdpi / 72f;
            case COMPLEX_UNIT_IN:
                return value * metrics.xdpi;
            case COMPLEX_UNIT_MM:
                return value * metrics.xdpi / 25.4f;
            default:
                return value;
        }
    }

    public float getDimension(DisplayMetrics metrics) {
        return applyDimension(this.data & COMPLEX_UNIT_MASK, complexToFloat(this.data), metrics);
    }

    public float getFraction(float base, float pbase) {
        float v = complexToFloat(this.data);
        return ((this.data >> COMPLEX_UNIT_SHIFT) & COMPLEX_UNIT_MASK) == COMPLEX_UNIT_FRACTION
                ? v * base : v * pbase;
    }

    public static float complexToFloat(int complex) {
        return (complex & (COMPLEX_MANTISSA_MASK << COMPLEX_MANTISSA_SHIFT))
                * RADIX_MULTS[(complex >> COMPLEX_RADIX_SHIFT) & COMPLEX_RADIX_MASK];
    }

    private static final float[] RADIX_MULTS = {
            1.0f * (1.0f / (1 << 8)),
            1.0f / (1 << 7),
            1.0f / (1 << 15),
            1.0f / (1 << 23)
    };

    public static String complexToDimensionPixelSize(int complex, DisplayMetrics metrics) {
        return Float.toString(complexToDimension(complex, metrics));
    }

    public static float complexToDimension(int complex, DisplayMetrics metrics) {
        return applyDimension((complex >> COMPLEX_UNIT_SHIFT) & COMPLEX_UNIT_MASK,
                complexToFloat(complex), metrics);
    }

    public final CharSequence coerceToString() {
        if (this.type == TYPE_STRING) return this.string;
        return toString();
    }

    @Override
    public String toString() {
        return "TypedValue{t=0x" + Integer.toHexString(this.type)
                + " d=0x" + Integer.toHexString(this.data)
                + " r=0x" + Integer.toHexString(this.resourceId) + "}";
    }
}
