package android.graphics.drawable.shapes;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;

/** android.graphics.drawable.shapes.RoundRectShape（AOSP 公开面）。 */
public class RoundRectShape extends RectShape {

    private final float[] mOuterRadii;
    private final RectF mInset;
    private final float[] mInnerRadii;

    public RoundRectShape(float[] outerRadii, RectF inset, float[] innerRadii) {
        mOuterRadii = outerRadii;
        mInset = inset;
        mInnerRadii = innerRadii;
    }

    public float[] getOuterRadii() {
        return mOuterRadii;
    }

    public RectF getInset() {
        return mInset;
    }

    public float[] getInnerRadii() {
        return mInnerRadii;
    }

    @Override
    public void draw(Canvas canvas, Paint paint) {
    }
}
