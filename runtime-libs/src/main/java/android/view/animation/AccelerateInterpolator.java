package android.view.animation;

import android.content.Context;
import android.util.AttributeSet;

/** android.view.animation.AccelerateInterpolator（AOSP 公开面）。 */
public class AccelerateInterpolator implements Interpolator {

    private final float mFactor;
    private final double mDoubleFactor;

    public AccelerateInterpolator() {
        mFactor = 1.0f;
        mDoubleFactor = 2.0;
    }

    public AccelerateInterpolator(float factor) {
        mFactor = factor;
        mDoubleFactor = 2 * mFactor;
    }

    public AccelerateInterpolator(Context context, AttributeSet attrs) {
        mFactor = 1.0f;
        mDoubleFactor = 2.0;
    }

    @Override
    public float getInterpolation(float input) {
        if (mFactor == 1.0f) {
            return input * input;
        }
        return (float) Math.pow(input, mDoubleFactor);
    }
}
