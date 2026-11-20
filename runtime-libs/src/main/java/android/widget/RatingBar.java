package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 评分条（AOSP android.widget.RatingBar 公开面；§31.7 C 档 lib）。
 *
 * 星级绘制由宿主物化（Android 原生 RatingBar）；rating/numStars 经快照
 * props 同步，宿主拖星经 UI 桥回灌 onHostRatingChanged（fromUser=true）。
 */
public class RatingBar extends ProgressBar {

    public interface OnRatingBarChangeListener {
        void onRatingChanged(RatingBar ratingBar, float rating, boolean fromUser);
    }

    private int mNumStars = 5;
    private float mRating;
    private float mStepSize = 0.5f;
    private boolean mIsIndicator;
    private OnRatingBarChangeListener mOnRatingBarChangeListener;

    public RatingBar(Context context) {
        super(context);
    }

    public RatingBar(Context context, AttributeSet attrs) {
        this(context);
    }

    public RatingBar(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }

    public void setNumStars(int numStars) {
        this.mNumStars = numStars < 0 ? 0 : numStars;
        if (this.mRating > this.mNumStars) {
            setRating(this.mNumStars);
        }
    }

    public int getNumStars() {
        return this.mNumStars;
    }

    public void setRating(float rating) {
        setRatingInternal(rating, false);
    }

    public float getRating() {
        return this.mRating;
    }

    public void setStepSize(float stepSize) {
        this.mStepSize = stepSize < 0 ? 0 : stepSize;
    }

    public float getStepSize() {
        return this.mStepSize;
    }

    public void setIsIndicator(boolean isIndicator) {
        this.mIsIndicator = isIndicator;
    }

    public boolean isIndicator() {
        return this.mIsIndicator;
    }

    public void setOnRatingBarChangeListener(OnRatingBarChangeListener listener) {
        this.mOnRatingBarChangeListener = listener;
    }

    public OnRatingBarChangeListener getOnRatingBarChangeListener() {
        return this.mOnRatingBarChangeListener;
    }

    /** 宿主拖星回灌（UI 桥 intrinsic 调用，非 AOSP 公开面）。 */
    public void onHostRatingChanged(float rating) {
        setRatingInternal(rating, true);
    }

    private void setRatingInternal(float rating, boolean fromUser) {
        float clamped = rating;
        if (clamped < 0) {
            clamped = 0;
        }
        if (clamped > this.mNumStars) {
            clamped = this.mNumStars;
        }
        if (this.mStepSize > 0 && this.mNumStars > 0) {
            float steps = Math.round(clamped / this.mStepSize);
            clamped = steps * this.mStepSize;
            if (clamped > this.mNumStars) {
                clamped = this.mNumStars;
            }
        }
        boolean changed = clamped != this.mRating;
        this.mRating = clamped;
        if ((fromUser || changed) && this.mOnRatingBarChangeListener != null) {
            this.mOnRatingBarChangeListener.onRatingChanged(this, clamped, fromUser);
        }
    }
}
