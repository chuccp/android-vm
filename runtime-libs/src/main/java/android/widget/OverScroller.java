package android.widget;

import android.content.Context;
import android.view.animation.Interpolator;

/**
 * 弹性滚动器（AOSP android.widget.OverScroller 公开面；§31.7 C 档 lib——纯
 * 数学/状态机，无 VM 布局需求）。桥接渲染不做帧动画，这里按 AOSP 状态迁进口径
 * 维护位置/速度（computeScrollOffset 按时间线性推进；fling 衰减简易实现）。
 */
public class OverScroller {
    private final SplineOverScroller mScrollerX = new SplineOverScroller();
    private final SplineOverScroller mScrollerY = new SplineOverScroller();
    public static final int FLING_MODE = 0;
    public static final int SCROLL_MODE = 1;

    static class SplineOverScroller {
        int mMode;
        int mStart;
        int mFinal;
        int mCurrentPosition;
        long mStartTime;
        int mDuration;
        float mDurationReciprocal;
        float mVelocity;

        void startScroll(int start, int distance, int duration, boolean flywheel) {
            mMode = SCROLL_MODE;
            mStart = start;
            mFinal = start + distance;
            mCurrentPosition = start;
            mStartTime = System.currentTimeMillis();
            mDuration = duration;
            mDurationReciprocal = 1.0f / duration;
        }

        void fling(int start, int velocity, int min, int max, int over) {
            mMode = FLING_MODE;
            mStart = start;
            mCurrentPosition = start;
            mFinal = velocity >= 0 ? max : min;
            mVelocity = velocity;
            mStartTime = System.currentTimeMillis();
            mDuration = Math.min(Math.abs(velocity) / 4, 3000);
        }

        boolean computeScrollOffset() {
            int timePassed = (int) (System.currentTimeMillis() - mStartTime);
            if (mMode == SCROLL_MODE) {
                if (mDuration == 0 || timePassed >= mDuration) {
                    mCurrentPosition = mFinal;
                    return false;
                }
                float t = timePassed * mDurationReciprocal;
                mCurrentPosition = mStart + Math.round((mFinal - mStart) * t);
                return true;
            }
            // FLING：速度线性衰减近似
            if (timePassed >= mDuration) {
                mCurrentPosition = mFinal;
                return false;
            }
            float t = (float) timePassed / mDuration;
            mCurrentPosition = mStart + Math.round((mFinal - mStart) * t);
            return true;
        }

        void abortAnimation() {
            mCurrentPosition = mFinal;
            mMode = -1;
        }
    }

    public OverScroller(Context context) {
    }

    public OverScroller(Context context, Interpolator interpolator) {
    }

    public final void startScroll(int startX, int startY, int dx, int dy, int duration) {
        mScrollerX.startScroll(startX, dx, duration, false);
        mScrollerY.startScroll(startY, dy, duration, false);
    }

    public final void startScroll(int startX, int startY, int dx, int dy) {
        startScroll(startX, startY, dx, dy, 250);
    }

    public final boolean computeScrollOffset() {
        boolean moreX = mScrollerX.computeScrollOffset();
        boolean moreY = mScrollerY.computeScrollOffset();
        return moreX || moreY;
    }

    public final boolean isFinished() {
        return !computeScrollOffset();
    }

    public final void abortAnimation() {
        mScrollerX.abortAnimation();
        mScrollerY.abortAnimation();
    }

    public final int getCurrX() {
        return mScrollerX.mCurrentPosition;
    }

    public final int getCurrY() {
        return mScrollerY.mCurrentPosition;
    }

    public final float getCurrVelocity() {
        return Math.max(Math.abs(mScrollerX.mVelocity), Math.abs(mScrollerY.mVelocity));
    }

    public final int getStartX() {
        return mScrollerX.mStart;
    }

    public final int getStartY() {
        return mScrollerY.mStart;
    }

    public final int getFinalX() {
        return mScrollerX.mFinal;
    }

    public final int getFinalY() {
        return mScrollerY.mFinal;
    }

    public final void fling(int startX, int startY, int velocityX, int velocityY,
                            int minX, int maxX, int minY, int maxY) {
        mScrollerX.fling(startX, velocityX, minX, maxX, 0);
        mScrollerY.fling(startY, velocityY, minY, maxY, 0);
    }

    public final void fling(int startX, int startY, int velocityX, int velocityY,
                            int minX, int maxX, int minY, int maxY, int overX, int overY) {
        fling(startX, startY, velocityX, velocityY, minX, maxX, minY, maxY);
    }

    public void forceFinished(boolean finished) {
        if (finished) abortAnimation();
    }

    public final int getDuration() {
        return Math.max(mScrollerX.mDuration, mScrollerY.mDuration);
    }

    @Override
    public String toString() {
        return "OverScroller(" + getCurrX() + "," + getCurrY() + ")";
    }
}
