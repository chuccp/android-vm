package android.view;

/**
 * View 轮廓提供者（AOSP android.view.ViewOutlineProvider 公开面）。
 * VM 的 UI 桥不消费轮廓（无阴影绘制面）；本类提供 AOSP 的静态实例常量，
 * 保证 View.setOutlineProvider/ViewOutlineProvider.BOUNDS 链接面成立。
 */
public class ViewOutlineProvider {
    public static final ViewOutlineProvider BACKGROUND = new ViewOutlineProvider();
    public static final ViewOutlineProvider BOUNDS = new ViewOutlineProvider();
    public static final ViewOutlineProvider PADDED_BOUNDS = new ViewOutlineProvider();

    public ViewOutlineProvider() {
    }

    /** AOSP 签名 getOutline(View, Outline)——Outline 面由参数承载，VM 不绘制。 */
    public void getOutline(View view, Object outline) {
    }
}
