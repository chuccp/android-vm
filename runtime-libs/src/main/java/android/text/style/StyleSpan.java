package android.text.style;

import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.Parcel;
import android.text.ParcelableSpan;
import android.text.TextPaint;

/**
 * 粗体/斜体 span（AOSP android.text.style.StyleSpan 形状）。
 * 生成补源：启动期 app 工具类 <clinit> 需要本类（缺失会导致
 * ExceptionInInitializerError 级联）。
 */
public class StyleSpan extends MetricAffectingSpan implements ParcelableSpan {

    private final int mStyle;

    public StyleSpan(int style) {
        mStyle = style;
    }

    public StyleSpan(Parcel src) {
        mStyle = src.readInt();
    }

    @Override
    public int getSpanTypeId() {
        return 1;  // AOSP TEXT_STYLE_SPAN
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(mStyle);
    }

    public int getStyle() {
        return mStyle;
    }

    @Override
    public void updateDrawState(TextPaint ds) {
        apply(ds, mStyle);
    }

    @Override
    public void updateMeasureState(TextPaint paint) {
        apply(paint, mStyle);
    }

    private static void apply(Paint paint, int style) {
        Typeface tf = paint.getTypeface();
        int oldStyle = tf != null ? tf.getStyle() : Typeface.NORMAL;
        int want = oldStyle | style;
        Typeface styled = Typeface.create(tf, want);
        paint.setTypeface(styled);
        if ((style & Typeface.BOLD) != 0) {
            paint.setFakeBoldText(true);
        }
    }
}
