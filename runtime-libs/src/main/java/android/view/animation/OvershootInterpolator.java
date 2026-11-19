package android.view.animation;

import android.content.Context;
import android.util.AttributeSet;

/** android.view.animation.OvershootInterpolator（AOSP 公开面）。 */
public class OvershootInterpolator implements Interpolator {

    private final float mTension;

    public OvershootInterpolator() {
        mTension = 2.0f;
    }

    public OvershootInterpolator(float tension) {
        mTension = tension;
    }

    public OvershootInterpolator(Context context, AttributeSet attrs) {
        mTension = 2.0f;
    }

    @Override
    public float getInterpolation(float t) {
        t -= 1.0f;
        return t * t * ((mTension + 1) * t + mTension) + 1.0f;
    }
}
