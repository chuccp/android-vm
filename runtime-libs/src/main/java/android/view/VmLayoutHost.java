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

    /**
     * PopupMenu 弹层登记（实现：libdexvm.so intrinsic）。anchor 为锚点视图
     * （guest mId 用于宿主解析真视图）；titles 为菜单项标题镜像。宿主用真
     * android.widget.PopupMenu 出画，点击经 nativeDispatchPopupEvent 回灌
     * {@code onHostMenuItemClick(index)}。
     */
    public static void showPopupMenu(Object popup, View anchor, String[] titles) {
        throw new UnsupportedOperationException("VmLayoutHost: VM intrinsic");
    }

    /** PopupMenu 摘除（dismiss）。 */
    public static void dismissPopupMenu(Object popup) {
        throw new UnsupportedOperationException("VmLayoutHost: VM intrinsic");
    }

    /**
     * PopupWindow 叠加窗口登记（实现：libdexvm.so intrinsic）。内容树成为
     * 独立叠加根（无 dim），(x,y) 为 guest 逻辑坐标定位；宿主物化浮层卡片，
     * 外点关闭经 nativeDispatchPopupEvent(token,1,0) 回灌
     * {@code onHostDismissed()}。
     */
    public static void showPopupWindow(Object popup, View content, int x, int y, int width,
                                       int height) {
        throw new UnsupportedOperationException("VmLayoutHost: VM intrinsic");
    }

    /** PopupWindow 叠加窗口摘除。 */
    public static void dismissPopupWindow(Object popup) {
        throw new UnsupportedOperationException("VmLayoutHost: VM intrinsic");
    }
}
