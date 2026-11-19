package android.graphics;

/**
 * android.graphics.PorterDuffXfermode（AOSP 公开面）。
 *
 * <p>Paint.setXfermode 的常用实参；本 VM 的合成光栅化语义后置，本类保留
 * 公开字段与构造器（AOSP：public final Mode mode）。
 */
public class PorterDuffXfermode extends Xfermode {

    public final PorterDuff.Mode mode;

    public PorterDuffXfermode(PorterDuff.Mode mode) {
        this.mode = mode;
    }
}
