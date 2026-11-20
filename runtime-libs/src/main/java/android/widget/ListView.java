package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

/**
 * 列表视图（AOSP android.widget.ListView 公开面；§31.7 C 档 lib）。
 *
 * VM 面：adapter 数据在 setAdapter 时物化为 item 子 View（adapter.getView），
 * onMeasure 逐项测量、onLayout 纵向堆叠——宿主 UI 桥按 guest 子 View 树物化
 * 真 TextView 行。滚动/复用（Recycler）暂为后续面（ListView 自身高度固定，
 * 超出裁剪）。
 */
public class ListView extends AbsListView {

    public interface OnItemClickListener extends AdapterView.OnItemClickListener {
    }

    public ListView(Context context) {
        super(context);
    }

    public ListView(Context context, AttributeSet attrs) {
        this(context);
    }

    public ListView(Context context, AttributeSet attrs, int defStyleAttr) {
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

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int width = View.MeasureSpec.getSize(widthMeasureSpec);
        int total = getPaddingTop() + getPaddingBottom();
        int count = getChildCount();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == View.GONE) {
                continue;
            }
            child.measure(
                    View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                    View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
            total += child.getMeasuredHeight();
        }
        setMeasuredDimension(
                View.resolveSize(width, widthMeasureSpec),
                View.resolveSize(total, heightMeasureSpec));
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int y = getPaddingTop();
        int x = getPaddingLeft();
        int count = getChildCount();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == View.GONE) {
                continue;
            }
            int w = child.getMeasuredWidth();
            int h = child.getMeasuredHeight();
            child.layout(x, y, x + w, y + h);
            y += h;
        }
    }

    public void addHeaderView(View v) {
    }

    public void addHeaderView(View v, Object data, boolean isSelectable) {
    }

    public void addFooterView(View v) {
    }

    public void addFooterView(View v, Object data, boolean isSelectable) {
    }

    public int getHeaderViewsCount() {
        return 0;
    }

    public int getFooterViewsCount() {
        return 0;
    }

    public boolean removeHeaderView(View v) {
        return false;
    }

    public boolean removeFooterView(View v) {
        return false;
    }

    public void setSelection(int position) {
    }

    public void setSelectionFromTop(int position, int y) {
    }

    public void setSelectionAfterHeaderView() {
    }

    public void setItemChecked(int position, boolean value) {
    }

    public void setItemsCanFocus(boolean itemsCanFocus) {
    }

    public boolean getItemsCanFocus() {
        return false;
    }
}
