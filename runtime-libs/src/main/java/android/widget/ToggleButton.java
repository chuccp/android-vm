package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 开关按钮（AOSP android.widget.ToggleButton 公开面；§31.7 C 档 lib）。
 *
 * textOn/textOff 在 checked 状态变化时映射为按钮文本（AOSP onDraw 语义的
 * 文本等价面；VM 无 drawable 状态机）。
 */
public class ToggleButton extends CompoundButton {

    private CharSequence mTextOn;
    private CharSequence mTextOff;

    public ToggleButton(Context context) {
        super(context);
    }

    public ToggleButton(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public ToggleButton(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void setTextOn(CharSequence text) {
        this.mTextOn = text;
        syncText();
    }

    public CharSequence getTextOn() {
        return this.mTextOn;
    }

    public void setTextOff(CharSequence text) {
        this.mTextOff = text;
        syncText();
    }

    public CharSequence getTextOff() {
        return this.mTextOff;
    }

    @Override
    public void setChecked(boolean checked) {
        super.setChecked(checked);
        syncText();
    }

    private void syncText() {
        CharSequence text = isChecked() ? this.mTextOn : this.mTextOff;
        if (text != null) {
            setText(text);
        }
    }
}
