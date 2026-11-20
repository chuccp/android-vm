package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 表格（AOSP android.widget.TableLayout 公开面；§31.7 C 档 lib）。
 *
 * VM 面：以纵向 LinearLayout 承载 TableRow（横向 LinearLayout）——现有
 * LLOnMeasure/LLOnLayout 直接复用；列对齐/stretch 等高级语义为后续面。
 */
public class TableLayout extends LinearLayout {

    private boolean mShrinkAllColumns;
    private boolean mStretchAllColumns;

    public TableLayout(Context context) {
        super(context, null);
        setOrientation(VERTICAL);
    }

    public TableLayout(Context context, AttributeSet attrs) {
        this(context);
    }

    public TableLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }

    public void setColumnCollapsed(int columnIndex, boolean collapsed) {
    }

    public boolean isColumnCollapsed(int columnIndex) {
        return false;
    }

    public void setColumnStretchable(int columnIndex, boolean stretchable) {
    }

    public boolean isColumnStretchable(int columnIndex) {
        return false;
    }

    public void setColumnShrinkable(int columnIndex, boolean shrinkable) {
    }

    public boolean isColumnShrinkable(int columnIndex) {
        return false;
    }

    public void setShrinkAllColumns(boolean shrinkAllColumns) {
        this.mShrinkAllColumns = shrinkAllColumns;
    }

    public boolean isShrinkAllColumns() {
        return this.mShrinkAllColumns;
    }

    public void setStretchAllColumns(boolean stretchAllColumns) {
        this.mStretchAllColumns = stretchAllColumns;
    }

    public boolean isStretchAllColumns() {
        return this.mStretchAllColumns;
    }
}
