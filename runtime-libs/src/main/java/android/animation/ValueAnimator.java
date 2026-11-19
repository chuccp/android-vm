package android.animation;

/**
 * 值动画（AOSP android.animation.ValueAnimator 公开面；§31.7 C 档 lib 骨架——
 * 值表/时长字段态；帧泵归桥接渲染层）。
 */
public class ValueAnimator extends Animator {

    /** 更新监听器（AOSP ValueAnimator.AnimatorUpdateListener）。 */
    public interface AnimatorUpdateListener {
        void onAnimationUpdate(ValueAnimator animation);
    }

    private float[] mValues;
    private Object mAnimatedValue;
    private long mDuration = 300;
    private TimeInterpolator mInterpolator;

    public ValueAnimator() {
    }

    public static ValueAnimator ofFloat(float... values) {
        ValueAnimator a = new ValueAnimator();
        a.mValues = values;
        return a;
    }

    public static ValueAnimator ofInt(int... values) {
        ValueAnimator a = new ValueAnimator();
        return a;
    }

    public static ValueAnimator ofObject(TypeEvaluator evaluator, Object... values) {
        ValueAnimator a = new ValueAnimator();
        return a;
    }

    /** 类型求值器（AOSP TypeEvaluator 接口面）。 */
    public interface TypeEvaluator<T> {
        T evaluate(float fraction, T startValue, T endValue);
    }

    public void setValues(float... values) {
        this.mValues = values;
    }

    public float[] getValues() {
        return this.mValues;
    }

    public Object getAnimatedValue() {
        return this.mAnimatedValue;
    }

    public Object getAnimatedValue(int index) {
        return this.mAnimatedValue;
    }

    @Override
    public ValueAnimator setDuration(long duration) {
        this.mDuration = duration;
        return this;
    }

    @Override
    public long getDuration() {
        return this.mDuration;
    }

    @Override
    public void setInterpolator(TimeInterpolator value) {
        this.mInterpolator = value;
    }

    @Override
    public TimeInterpolator getInterpolator() {
        return this.mInterpolator;
    }

    public void setCurrentFraction(float fraction) {
    }

    public float getAnimatedFraction() {
        return 0;
    }

    public void addUpdateListener(AnimatorUpdateListener listener) {
    }

    public void removeUpdateListener(AnimatorUpdateListener listener) {
    }

    /** AOSP 公开面：清空全部更新监听器。 */
    public void removeAllUpdateListeners() {
    }

    public void setRepeatCount(int value) {
    }

    public void setRepeatMode(int value) {
    }

    public static void setFrameDelay(long frameDelay) {
    }

    public void setStartDelay(long startDelay) {
    }
}
