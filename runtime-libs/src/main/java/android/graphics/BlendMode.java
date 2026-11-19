package android.graphics;

/**
 * 混合模式（AOSP android.graphics.BlendMode 枚举；§31.7 C 档 lib POJO 枚举）。
 * 仅 Paint.setBlendMode 引用面载体；与 PorterDuff$Mode 双面并存（AOSP 同）。
 */
public enum BlendMode {
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
    PLUS,
    MODULATE,
    SCREEN,
    OVERLAY,
    DARKEN,
    LIGHTEN,
    COLOR_DODGE,
    COLOR_BURN,
    HARD_LIGHT,
    SOFT_LIGHT,
    DIFFERENCE,
    EXCLUSION,
    MULTIPLY,
    HUE,
    SATURATION,
    COLOR,
    LUMINOSITY
}
