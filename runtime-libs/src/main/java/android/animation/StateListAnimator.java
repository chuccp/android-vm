package android.animation;

/**
 * android.animation.StateListAnimator（AOSP 公开面）。
 *
 * <p>状态 -> Animator 映射；帧泵归桥接渲染层，本类保留注册/切换接口。
 */
public class StateListAnimator {

    public StateListAnimator() {
    }

    public void addState(int[] specs, Animator animator) {
    }

    public void setState(int[] state) {
    }

    public void jumpToCurrentState() {
    }

    public void setTarget(Object target) {
    }
}
