package android.content.res;

/**
 * VM 资产钩子（编译期脚手架）：实现由 libdexvm.so 内建类提供
 * （core/framework_builtins.cc 注册 intrinsic；builtin_classes.def 排除本类
 * 的 Java 版本，双实现零容忍——本文件仅让 lib 源码可编译）。
 */
public final class VmAssets {
    private VmAssets() {}

    /** assets/&lt;name&gt; -> 原始字节；查无返回 null（VM intrinsic）。 */
    public static byte[] open(String name) {
        throw new UnsupportedOperationException("VmAssets.open: VM intrinsic");
    }
}
