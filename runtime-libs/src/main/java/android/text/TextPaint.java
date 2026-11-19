package android.text;

import android.graphics.Paint;

/**
 * 文本画笔（AOSP android.text.TextPaint 公开面；§31.7 C 档 lib）。
 * Paint 的文本态扩展（density/linkColor/bgColor/baselineShift/drawableState
 * 字段态）；测量面继承 Paint 网格缝（render 管线对齐）。
 */
public class TextPaint extends Paint {
    public int linkColor = 0xFF056B05;
    public int bgColor;
    public int baselineShift;
    public float density = 1.0f;
    public int[] drawableState;

    public TextPaint() {
        super();
    }

    public TextPaint(int flags) {
        super(flags);
    }

    public TextPaint(Paint p) {
        super(p);
    }

    /** 从 another 复制文本态（AOSP TextPaint.set）。 */
    public void set(TextPaint another) {
        if (another == null) return;
        super.set(another);
        this.linkColor = another.linkColor;
        this.bgColor = another.bgColor;
        this.baselineShift = another.baselineShift;
        this.density = another.density;
        this.drawableState = another.drawableState;
    }
}
