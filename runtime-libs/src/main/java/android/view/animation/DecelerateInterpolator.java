package android.view.animation;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;

/**
 * android.view.animation.DecelerateInterpolator（AOSP 公开面）。
 */
public class DecelerateInterpolator implements Interpolator {

    private float mFactor = 1.0f;

    public DecelerateInterpolator() {
    }

    public DecelerateInterpolator(float factor) {
        mFactor = factor;
    }

    public DecelerateInterpolator(Context context, AttributeSet attrs) {
    }

    @Override
    public float getInterpolation(float input) {
        if (mFactor == 1.0f) {
            return (float) (1.0f - (1.0f - input) * (1.0f - input));
        }
        return (float) (Math.pow(1.0f - input, 2.0f * mFactor) * -1.0 + 1.0);
    }
}
