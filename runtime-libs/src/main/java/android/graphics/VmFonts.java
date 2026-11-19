package android.graphics;

/**
 * 字体桥钩子（编译期脚手架）：实现由 libdexvm.so 内建类提供
 * （core/framework_font.cc 注册 intrinsic；builtin_classes.def 排除本类
 * 的 Java 版本，双实现零容忍——本文件仅让 lib 源码可编译）。
 *
 * AOSP 口径：字体解析/回退链/shaping 与文本度量在平台 native（Minikin）；
 * 本桥把 Typeface 创建与 Paint 度量转给宿主真 Android 字体栈
 * （com.dexvm.host.FontBridge）。handle=0 表示平台默认字体。
 */
public final class VmFonts {
    private VmFonts() {}

    /** 宿主字体桥是否可用（桌面单测为 false，lib 回落网格度量）。 */
    public static boolean available() {
        throw new UnsupportedOperationException("VmFonts.available: VM intrinsic");
    }

    /** 系统字族（family 为空 = 默认字族）-> 宿主 Typeface 句柄。 */
    public static long createFromFamily(String family, int style) {
        throw new UnsupportedOperationException("VmFonts.createFromFamily: VM intrinsic");
    }

    /** 字体文件路径 -> 宿主 Typeface 句柄。 */
    public static long createFromFile(String path, int style) {
        throw new UnsupportedOperationException("VmFonts.createFromFile: VM intrinsic");
    }

    /** 在已有句柄上做风格化（AOSP Typeface.create(family, style)）。 */
    public static long restyle(long baseHandle, int style) {
        throw new UnsupportedOperationException("VmFonts.restyle: VM intrinsic");
    }

    /** APK font 资源 id -> 宿主 Typeface 句柄（VM 抽字节落盘后交宿主）。 */
    public static long createFromResource(int resId, int style) {
        throw new UnsupportedOperationException("VmFonts.createFromResource: VM intrinsic");
    }

    /** 文本宽度（px）；返回 -1 表示桥不可用（lib 回落网格）。 */
    public static float measureText(long handle, String text, float sizePx) {
        throw new UnsupportedOperationException("VmFonts.measureText: VM intrinsic");
    }

    /** FontMetricsInt：out[0..4] = top/ascent/descent/bottom/leading。 */
    public static boolean fontMetrics(long handle, float sizePx, int[] out) {
        throw new UnsupportedOperationException("VmFonts.fontMetrics: VM intrinsic");
    }
}
