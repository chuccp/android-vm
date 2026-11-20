package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

/**
 * 网格视图（AOSP android.widget.GridView 公开面；§31.7 C 档 lib）。
 *
 * VM 面同 ListView：adapter 数据在 setAdapter 时物化为 item 子 View；
 * onMeasure 按列数分行测量、onLayout 网格摆放——宿主 UI 桥按 guest 子 View
 * 树物化真 TextView 格子（滚动/复用为后续面）。
 */
public class GridView extends AbsListView {

    private int mNumColumns = 2;
    private int mHorizontalSpacing;
    private int mVerticalSpacing;
    private int mStretchMode = 2;  // STRETCH_COLUMN_WIDTH
    private int mColumnWidth;

    public GridView(Context context) {
        super(context);
    }

    public GridView(Context context, AttributeSet attrs) {
        this(context);
    }

    public GridView(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }

    @Override
    public void setAdapter(ListAdapter adapter) {
        super.setAdapter(adapter);
        rebuildChildren();
    }

    private void rebuildChildren() {
        super.removeAllViews();
        ListAdapter adapter = getAdapter();
        if (adapter == null) {
            requestLayout();
            return;
        }
        int count = adapter.getCount();
        for (int i = 0; i < count; i++) {
            View item = adapter.getView(i, null, this);
            if (item != null) {
                addView(item);
            }
        }
        requestLayout();
    }

    public void setNumColumns(int numColumns) {
        this.mNumColumns = numColumns < 1 ? 1 : numColumns;
        requestLayout();
    }

    public int getNumColumns() {
        return this.mNumColumns;
    }

    public void setHorizontalSpacing(int spacing) {
        this.mHorizontalSpacing = spacing;
    }

    public int getHorizontalSpacing() {
        return this.mHorizontalSpacing;
    }

    public void setVerticalSpacing(int spacing) {
        this.mVerticalSpacing = spacing;
    }

    public int getVerticalSpacing() {
        return this.mVerticalSpacing;
    }

    public void setStretchMode(int mode) {
        this.mStretchMode = mode;
    }

    public int getStretchMode() {
        return this.mStretchMode;
    }

    public void setColumnWidth(int width) {
        this.mColumnWidth = width;
    }

    public int getColumnWidth() {
        return this.mColumnWidth;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int width = View.MeasureSpec.getSize(widthMeasureSpec);
        int columns = mNumColumns < 1 ? 1 : mNumColumns;
        int contentWidth = width - getPaddingLeft() - getPaddingRight()
                - mHorizontalSpacing * (columns - 1);
        int cellWidth = contentWidth / columns;
        if (mColumnWidth > 0 && mColumnWidth < cellWidth) {
            cellWidth = mColumnWidth;
        }
        int totalHeight = getPaddingTop() + getPaddingBottom();
        int count = getChildCount();
        for (int i = 0; i < count; i += columns) {
            int rowHeight = 0;
            for (int j = 0; j < columns && i + j < count; j++) {
                View child = getChildAt(i + j);
                if (child == null || child.getVisibility() == View.GONE) {
                    continue;
                }
                child.measure(
                        View.MeasureSpec.makeMeasureSpec(cellWidth > 0 ? cellWidth : 0,
                                View.MeasureSpec.EXACTLY),
                        View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
                rowHeight = Math.max(rowHeight, child.getMeasuredHeight());
            }
            totalHeight += rowHeight;
            if (i + columns < count) {
                totalHeight += mVerticalSpacing;
            }
        }
        setMeasuredDimension(
                View.resolveSize(width, widthMeasureSpec),
                View.resolveSize(totalHeight, heightMeasureSpec));
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int columns = mNumColumns < 1 ? 1 : mNumColumns;
        int width = r - l;
        int contentWidth = width - getPaddingLeft() - getPaddingRight()
                - mHorizontalSpacing * (columns - 1);
        int cellWidth = contentWidth / columns;
        int x = getPaddingLeft();
        int y = getPaddingTop();
        int count = getChildCount();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == View.GONE) {
                continue;
            }
            int col = i % columns;
            if (col == 0 && i > 0) {
                y += rowHeightOf(i - columns, columns) + mVerticalSpacing;
                x = getPaddingLeft();
            }
            int w = child.getMeasuredWidth();
            int h = child.getMeasuredHeight();
            child.layout(x, y, x + w, y + h);
            x += w + mHorizontalSpacing;
        }
    }

    private int rowHeightOf(int rowStart, int columns) {
        int rowHeight = 0;
        for (int j = 0; j < columns; j++) {
            View child = getChildAt(rowStart + j);
            if (child == null || child.getVisibility() == View.GONE) {
                continue;
            }
            rowHeight = Math.max(rowHeight, child.getMeasuredHeight());
        }
        return rowHeight;
    }
}
