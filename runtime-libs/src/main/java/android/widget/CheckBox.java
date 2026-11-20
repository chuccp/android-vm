package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 复选框（AOSP android.widget.CheckBox 公开面；§31.7 C 档 lib）。
 *
 * 语义全部继承 CompoundButton（checked 状态 + OnCheckedChangeListener）；
 * 由资源布局 tag "CheckBox" 经 framework_inflate 的 guest 类解析实例化。
 */
public class CheckBox extends CompoundButton {

    public CheckBox(Context context) {
        super(context);
    }

    public CheckBox(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public CheckBox(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }
}
