package android.animation;

/**
 * 动画监听适配器（AOSP android.animation.AnimatorListenerAdapter；§31.7 C 档 lib）。
 */
public abstract class AnimatorListenerAdapter implements Animator.AnimatorListener {

    public AnimatorListenerAdapter() {
    }

    @Override
    public void onAnimationStart(Animator animation) {
    }

    @Override
    public void onAnimationEnd(Animator animation) {
    }

    @Override
    public void onAnimationCancel(Animator animation) {
    }

    @Override
    public void onAnimationRepeat(Animator animation) {
    }
}
