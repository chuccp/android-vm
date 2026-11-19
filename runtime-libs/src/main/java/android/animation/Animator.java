package android.animation;

/**
 * 动画基类（AOSP android.animation.Animator 公开面；§31.7 C 档 lib 骨架——
 * 动画执行归桥接渲染层；本面为监听器/时长/目标字段态 + 空转启停）。
 */
public abstract class Animator implements Cloneable {

    /** 动画监听器（AOSP Animator.AnimatorListener）。 */
    public interface AnimatorListener {
        void onAnimationStart(Animator animation);

        void onAnimationEnd(Animator animation);

        void onAnimationCancel(Animator animation);

        void onAnimationRepeat(Animator animation);
    }

    public Animator() {
    }

    public void start() {
    }

    public void cancel() {
    }

    public void end() {
    }

    public long getDuration() {
        return 0;
    }

    public Animator setDuration(long duration) {
        return this;
    }

    public void setStartDelay(long startDelay) {
    }

    public long getStartDelay() {
        return 0;
    }

    public Animator setInterpolator(TimeInterpolator value) {
        return this;
    }

    public boolean isRunning() {
        return false;
    }

    public boolean isStarted() {
        return false;
    }

    public void setTarget(Object target) {
    }

    public void addListener(AnimatorListener listener) {
    }

    public void removeListener(AnimatorListener listener) {
    }

    public java.util.ArrayList<AnimatorListener> getListeners() {
        return null;
    }

    @Override
    public Animator clone() {
        try {
            return (Animator) super.clone();
        } catch (CloneNotSupportedException e) {
            throw new AssertionError(e);
        }
    }

    public void setupStartValues() {
    }

    public void setupEndValues() {
    }
}
