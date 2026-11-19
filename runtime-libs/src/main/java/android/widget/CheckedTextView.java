package android.widget;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;

/**
 * 可选中文本（AOSP android.widget.CheckedTextView 公开面；§31.7 C 档 lib——选中态）。
 */
public class CheckedTextView extends TextView {

    private boolean mChecked;

    public CheckedTextView(Context context) {
        super(context);
    }

    public CheckedTextView(Context context, AttributeSet attrs) {
        this(context);
    }

    public void setChecked(boolean checked) {
        this.mChecked = checked;
    }

    public boolean isChecked() {
        return this.mChecked;
    }

    public void toggle() {
        setChecked(!this.mChecked);
    }

    public void setCheckMarkDrawable(int resId) {
    }

    public void setCheckMarkDrawable(Drawable d) {
    }
}
