package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 按钮（AOSP android.widget.Button 公开面；§31.7 C 档 lib 骨架）。
 */
public class Button extends TextView {

    public Button(Context context) {
        super(context);
    }

    public Button(Context context, AttributeSet attrs) {
        this(context);
    }

    public Button(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }
}
