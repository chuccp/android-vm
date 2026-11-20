package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 表格行（AOSP android.widget.TableRow 公开面；§31.7 C 档 lib）。
 *
 * VM 面：横向 LinearLayout 子项流式摆放（列宽不做跨行对齐，为后续面）。
 */
public class TableRow extends LinearLayout {

    public TableRow(Context context) {
        super(context, null);
        setOrientation(HORIZONTAL);
    }

    public TableRow(Context context, AttributeSet attrs) {
        this(context);
    }

    public TableRow(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }
}
