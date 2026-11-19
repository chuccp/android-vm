package android.view;

/**
 * VM 平台布局钩子（编译期脚手架）：实现由 libdexvm.so 内建类提供
 * （core/framework_builtins.cc 注册 intrinsic；builtin_classes.def 排除本类
 * 的 Java 版本，双实现零容忍——本文件仅让 lib 源码可编译）。
 */
public final class VmLayoutHost {
    private VmLayoutHost() {}

    public static View inflate(LayoutInflater inflater, int resource, ViewGroup root,
                               boolean attachToRoot) {
        throw new UnsupportedOperationException("VmLayoutHost: VM intrinsic");
    }
}
