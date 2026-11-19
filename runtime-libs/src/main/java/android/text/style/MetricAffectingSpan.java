package android.text.style;

import android.text.TextPaint;

/**
 * 影响度量（字号/字重等）的 span 基类（AOSP
 * android.text.style.MetricAffectingSpan 形状）。
 */
public abstract class MetricAffectingSpan extends CharacterStyle {

    public abstract void updateMeasureState(TextPaint p);

    @Override
    public MetricAffectingSpan getUnderlying() {
        return this;
    }
}
