package android.view.animation;

import android.content.Context;
import android.util.AttributeSet;

/** android.view.animation.LinearInterpolator（AOSP 公开面）。 */
public class LinearInterpolator implements Interpolator {

    public LinearInterpolator() {
    }

    public LinearInterpolator(Context context, AttributeSet attrs) {
    }

    @Override
    public float getInterpolation(float input) {
        return input;
    }
}
