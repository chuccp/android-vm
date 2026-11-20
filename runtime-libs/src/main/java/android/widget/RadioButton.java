package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 单选按钮（AOSP android.widget.RadioButton 公开面；§31.7 C 档 lib）。
 *
 * AOSP 语义：已选中的按钮再次点击不取消（toggle 守卫），互斥由父
 * RadioGroup 的 OnCheckedChangeListener 维护。
 */
public class RadioButton extends CompoundButton {

    public RadioButton(Context context) {
        super(context);
    }

    public RadioButton(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public RadioButton(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    @Override
    public void toggle() {
        if (!isChecked()) {
            super.toggle();
        }
    }
}
