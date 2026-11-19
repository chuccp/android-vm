package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 图片按钮（AOSP android.widget.ImageButton 公开面；§31.7 C 档 lib 骨架）。
 */
public class ImageButton extends ImageView {

    public ImageButton(Context context) {
        super(context);
    }

    public ImageButton(Context context, AttributeSet attrs) {
        this(context);
    }

    public ImageButton(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }
}
