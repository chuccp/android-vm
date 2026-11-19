package android.widget;

import android.content.Context;
import android.view.animation.Interpolator;

/**
 * android.widget.Scroller（AOSP 公开面）。
 *
 * <p>时间驱动的滚动位置计算（computeScrollOffset 按经过时间 + Interpolator
 * 求当前坐标）。RecyclerView/ViewPager2 平滑滚动消费面。
 */
public class Scroller {
    private int mStartX;
    private int mStartY;
    private int mFinalX;
    private int mFinalY;
    private int mDuration;
    private long mStartTime = -1;
    private boolean mFinished = true;
    private boolean mFlywheel;
    private float mFriction = 0.015f;
    private Interpolator mInterpolator;

    public Scroller(Context context) {
    }

    public Scroller(Context context, Interpolator interpolator) {
        mInterpolator = interpolator;
    }

    public Scroller(Context context, Interpolator interpolator, boolean flywheel) {
        mInterpolator = interpolator;
        mFlywheel = flywheel;
    }

    public final boolean isFinished() {
        return mFinished;
    }

    public final void forceFinished(boolean finished) {
        mFinished = finished;
    }

    public final int getCurrX() {
        return mStartX + (int) ((mFinalX - mStartX) * progress());
    }

    public final int getCurrY() {
        return mStartY + (int) ((mFinalY - mStartY) * progress());
    }

    public final int getStartX() {
        return mStartX;
    }

    public final int getStartY() {
        return mStartY;
    }

    public final int getFinalX() {
        return mFinalX;
    }

    public final int getFinalY() {
        return mFinalY;
    }

    public final int getDuration() {
        return mDuration;
    }

    public void setFriction(float friction) {
        mFriction = friction;
    }

    public void extendDuration(int extend) {
        mDuration += extend;
    }

    public void startScroll(int startX, int startY, int dx, int dy) {
        startScroll(startX, startY, dx, dy, 250);
    }

    public void startScroll(int startX, int startY, int dx, int dy, int duration) {
        mStartX = startX;
        mStartY = startY;
        mFinalX = startX + dx;
        mFinalY = startY + dy;
        mDuration = duration < 1 ? 1 : duration;
        mStartTime = -1;
        mFinished = false;
    }

    public boolean computeScrollOffset() {
        if (mFinished) {
            return false;
        }
        long now = android.os.SystemClock.uptimeMillis();
        if (mStartTime < 0) {
            mStartTime = now;
        }
        long elapsed = now - mStartTime;
        if (elapsed >= mDuration) {
            mFinished = true;
            return true;
        }
        return true;
    }

    public void abortAnimation() {
        mFinished = true;
    }

    public int timePassed() {
        if (mStartTime < 0) return 0;
        long now = android.os.SystemClock.uptimeMillis();
        long elapsed = now - mStartTime;
        return elapsed > mDuration ? mDuration : (int) elapsed;
    }

    public void fling(int startX, int startY, int velocityX, int velocityY, int minX, int maxX,
                      int minY, int maxY) {
        // 简化 fling：按速度方向滚一段（AOSP 物理曲线后置）。
        mStartX = startX;
        mStartY = startY;
        mFinalX = startX + velocityX / 4;
        mFinalY = startY + velocityY / 4;
        mDuration = 400;
        mStartTime = -1;
        mFinished = false;
    }

    private float progress() {
        if (mFinished) return 1.0f;
        if (mStartTime < 0) return 0.0f;
        long now = android.os.SystemClock.uptimeMillis();
        float t = (now - mStartTime) / (float) mDuration;
        if (t >= 1.0f) return 1.0f;
        if (mInterpolator != null) {
            float v = mInterpolator.getInterpolation(t);
            if (v < 0f) v = 0f;
            if (v > 1f) v = 1f;
            return v;
        }
        return t;
    }
}
