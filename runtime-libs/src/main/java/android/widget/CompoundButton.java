package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

/**
 * 复合按钮（AOSP android.widget.CompoundButton 公开面；§31.7 C 档 lib——选中态）。
 */
public abstract class CompoundButton extends View {

    private boolean mChecked;
    private OnCheckedChangeListener mOnCheckedChangeListener;

    public interface OnCheckedChangeListener {
        void onCheckedChanged(CompoundButton buttonView, boolean isChecked);
    }

    public CompoundButton(Context context) {
        super(context);
    }

    public CompoundButton(Context context, AttributeSet attrs) {
        this(context);
    }

    public void toggle() {
        setChecked(!this.mChecked);
    }

    @Override
    public boolean performClick() {
        toggle();
        return super.performClick();
    }

    public boolean isChecked() {
        return this.mChecked;
    }

    public void setChecked(boolean checked) {
        if (this.mChecked != checked) {
            this.mChecked = checked;
            notifyCheckedChanged();
            if (this.mOnCheckedChangeListener != null) {
                this.mOnCheckedChangeListener.onCheckedChanged(this, this.mChecked);
            }
        }
    }

    public void setOnCheckedChangeListener(OnCheckedChangeListener listener) {
        this.mOnCheckedChangeListener = listener;
    }

    public void setOnCheckedChangeWidgetListener(OnCheckedChangeListener listener) {
    }

    /** drawable 状态刷新（View 缝面空转；命名避开 View.refreshDrawableState）。 */
    private void notifyCheckedChanged() {
    }

    public CharSequence getText() {
        return null;
    }

    public void setText(CharSequence text) {
    }
}
