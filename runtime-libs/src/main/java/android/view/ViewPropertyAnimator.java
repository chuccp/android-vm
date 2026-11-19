package android.view;

import android.animation.Animator;
import android.animation.TimeInterpolator;

/**
 * 视图属性动画（AOSP android.view.ViewPropertyAnimator 公开面；§31.7 C 档 lib
 * 骨架——链式 setter 面；动画执行归桥接渲染层，本面链式语义 + 监听器登记）。
 */
public class ViewPropertyAnimator {

    // 无参面保留给 VM 旧构造路径；绑定 View 后链式 setter 即时落到目标值
    // （VM 无帧泵：动画语义按"立即到达终态"近似，避免停在动画起始偏移）。
    private final View mView;

    public ViewPropertyAnimator() {
        this(null);
    }

    public ViewPropertyAnimator(View view) {
        this.mView = view;
    }

    private long mDuration = -1;
    private long mStartDelay = -1;

    public ViewPropertyAnimator setDuration(long duration) {
        this.mDuration = duration;
        return this;
    }

    public long getDuration() {
        return this.mDuration;
    }

    public ViewPropertyAnimator setStartDelay(long startDelay) {
        this.mStartDelay = startDelay;
        return this;
    }

    public long getStartDelay() {
        return this.mStartDelay;
    }

    public ViewPropertyAnimator setInterpolator(TimeInterpolator value) {
        return this;
    }

    public ViewPropertyAnimator setListener(Animator.AnimatorListener listener) {
        return this;
    }

    public ViewPropertyAnimator withEndAction(Runnable runnable) {
        return this;
    }

    public ViewPropertyAnimator withStartAction(Runnable runnable) {
        return this;
    }

    public ViewPropertyAnimator withLayer() {
        return this;
    }

    public ViewPropertyAnimator setUpdateListener(
            android.animation.ValueAnimator.AnimatorUpdateListener listener) {
        return this;
    }

    public ViewPropertyAnimator alpha(float value) {
        if (mView != null) mView.setAlpha(value);
        return this;
    }

    public ViewPropertyAnimator translationX(float value) {
        if (mView != null) mView.setTranslationX(value);
        return this;
    }

    public ViewPropertyAnimator translationY(float value) {
        if (mView != null) mView.setTranslationY(value);
        return this;
    }

    public ViewPropertyAnimator translationZ(float value) {
        return this;
    }

    public ViewPropertyAnimator scaleX(float value) {
        return this;
    }

    public ViewPropertyAnimator scaleY(float value) {
        return this;
    }

    public ViewPropertyAnimator rotation(float value) {
        return this;
    }

    public ViewPropertyAnimator rotationX(float value) {
        return this;
    }

    public ViewPropertyAnimator rotationY(float value) {
        return this;
    }

    public ViewPropertyAnimator x(float value) {
        if (mView != null) mView.setTranslationX(value - mView.getLeft());
        return this;
    }

    public ViewPropertyAnimator y(float value) {
        if (mView != null) mView.setTranslationY(value - mView.getTop());
        return this;
    }

    public ViewPropertyAnimator z(float value) {
        return this;
    }

    public void start() {
    }

    public void cancel() {
    }
}
