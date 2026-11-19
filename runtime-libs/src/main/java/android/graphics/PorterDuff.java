package android.graphics;

/**
 * Porter-Duff 合成（AOSP android.graphics.PorterDuff；§31.7 C 档 lib POJO）。
 */
public class PorterDuff {

    /** 合成模式枚举（AOSP PorterDuff.Mode）。 */
    public enum Mode {
        CLEAR,
        SRC,
        DST,
        SRC_OVER,
        DST_OVER,
        SRC_IN,
        DST_IN,
        SRC_OUT,
        DST_OUT,
        SRC_ATOP,
        DST_ATOP,
        XOR,
        DARKEN,
        LIGHTEN,
        MULTIPLY,
        SCREEN,
        ADD,
        OVERLAY
    }

    public PorterDuff() {
    }
}
