package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 复合按钮（AOSP android.widget.CompoundButton 公开面；§31.7 C 档 lib——选中态）。
 *
 * 层级按 AOSP 语义：CheckBox/RadioButton/ToggleButton 的公共基类承载 checked
 * 状态与 OnCheckedChangeListener。AOSP 上 CompoundButton extends Button
 * （TextView 族）；这里直接 extends TextView，保证文本测量/绘制（TextView
 * 内建 intrinsic）与 setText/getText 可用（旧骨架 extends View 无文字面）。
 *
 * 点击语义：宿主点击回灌经 performClick()（AOSP 触摸口径）——先 toggle 再
 * 走 View.performClick 派发 OnClickListener 与 OnCheckedChangeListener。
 */
public abstract class CompoundButton extends TextView implements Checkable {

    public interface OnCheckedChangeListener {
        void onCheckedChanged(CompoundButton buttonView, boolean isChecked);
    }

    private boolean mChecked;
    private OnCheckedChangeListener mOnCheckedChangeListener;

    public CompoundButton(Context context) {
        super(context);
    }

    public CompoundButton(Context context, AttributeSet attrs) {
        this(context);
    }

    public CompoundButton(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }

    @Override
    public void toggle() {
        setChecked(!this.mChecked);
    }

    @Override
    public boolean performClick() {
        toggle();
        return super.performClick();
    }

    @Override
    public boolean isChecked() {
        return this.mChecked;
    }

    @Override
    public void setChecked(boolean checked) {
        if (this.mChecked != checked) {
            this.mChecked = checked;
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
}
