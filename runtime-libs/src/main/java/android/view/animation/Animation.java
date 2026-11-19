package android.view.animation;

import android.view.View;

/**
 * 补间动画（AOSP android.view.animation.Animation 公开面；§31.7 C 档 lib 骨架
 * ——变换矩阵态；帧泵归桥接渲染层）。
 */
public abstract class Animation {

    public interface AnimationListener {
        void onAnimationStart(Animation animation);

        void onAnimationEnd(Animation animation);

        void onAnimationRepeat(Animation animation);
    }

    public static final int RELATIVE_TO_SELF = 0;
    public static final int RELATIVE_TO_PARENT = 1;
    public static final int ABSOLUTE = 2;
    public static final int INFINITE = -1;
    public static final int RESTART = 1;
    public static final int REVERSE = 2;
    public static final int START_ON_FIRST_FRAME = -1;

    protected long mStartOffset;
    protected long mDuration = 0;
    protected boolean mFillAfter;
    protected boolean mFillBefore = true;
    protected boolean mFillEnabled;
    protected int mRepeatCount;
    protected int mRepeatMode = RESTART;
    protected Interpolator mInterpolator;
    private AnimationListener mListener;

    public Animation() {
    }

    public void reset() {
    }

    public void cancel() {
    }

    public long getStartOffset() {
        return this.mStartOffset;
    }

    public void setStartOffset(long startOffset) {
        this.mStartOffset = startOffset;
    }

    public long getDuration() {
        return this.mDuration;
    }

    public void setDuration(long durationMillis) {
        this.mDuration = durationMillis;
    }

    public long getStartTime() {
        return 0;
    }

    public void setStartTime(long startTimeMillis) {
    }

    public void restrictDuration(long durationMillis) {
    }

    public void scaleCurrentDuration(float scale) {
        this.mDuration = (long) (this.mDuration * scale);
    }

    public void setFillBefore(boolean fillBefore) {
        this.mFillBefore = fillBefore;
    }

    public void setFillAfter(boolean fillAfter) {
        this.mFillAfter = fillAfter;
    }

    public void setFillEnabled(boolean fillEnabled) {
        this.mFillEnabled = fillEnabled;
    }

    public void setRepeatMode(int repeatMode) {
        this.mRepeatMode = repeatMode;
    }

    public void setRepeatCount(int repeatCount) {
        this.mRepeatCount = repeatCount;
    }

    public boolean isFillEnabled() {
        return this.mFillEnabled;
    }

    public boolean isInitialized() {
        return true;
    }

    public void initialize(int width, int height, int parentWidth, int parentHeight) {
    }

    public void setInterpolator(Interpolator i) {
        this.mInterpolator = i;
    }

    public void setInterpolator(android.content.Context context, int resID) {
    }

    public void setAnimationListener(AnimationListener listener) {
        this.mListener = listener;
    }

    protected void applyTransformation(float interpolatedTime, android.view.animation.Transformation t) {
    }

    public boolean willChangeBounds() {
        return true;
    }

    public boolean willChangeTransformationMatrix() {
        return true;
    }

    public boolean hasStarted() {
        return false;
    }

    public boolean hasEnded() {
        return true;
    }

    /** 变换载体（AOSP android.view.animation.Transformation）。 */
    public static class Transformation {
        protected float mAlpha = 1.0f;
        protected android.graphics.Matrix mMatrix = new android.graphics.Matrix();

        public Transformation() {
        }

        public void clear() {
        }

        public float getAlpha() {
            return this.mAlpha;
        }

        public void setAlpha(float alpha) {
            this.mAlpha = alpha;
        }

        public android.graphics.Matrix getMatrix() {
            return this.mMatrix;
        }

        public int getTransformationType() {
            return 0;
        }
    }
}
