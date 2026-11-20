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

    /**
     * Dialog 窗口登记（实现：libdexvm.so intrinsic）。内容树作为独立窗口根进入
     * 会话快照（叠加在 Activity 内容之上）；width/height 为 Window.setLayout 的
     * 意图（MATCH_PARENT=-1 / WRAP_CONTENT=-2 / px），gravity 默认 -1 = 居中。
     */
    public static void showDialog(Object dialog, View content, int width, int height,
                                  int gravity, float dimAmount, boolean cancelable) {
        throw new UnsupportedOperationException("VmLayoutHost: VM intrinsic");
    }

    /** Dialog 窗口摘除（dismiss/hide）。 */
    public static void dismissDialog(Object dialog) {
        throw new UnsupportedOperationException("VmLayoutHost: VM intrinsic");
    }
}
