package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

/**
 * 下拉选择器（AOSP android.widget.Spinner 公开面；§31.7 C 档 lib）。
 *
 * VM 面：无弹窗窗口/ListPopupWindow 面，交互近似为"点击循环选中下一项"——
 * 选中项由 adapter.getView 物化为单个子 View，点击（宿主 performClick 回灌）
 * 推进到下一项并派发 OnItemSelectedListener。视觉/位置语义与 AOSP 的收起态
 * 一致（当前选中项文本）。
 */
public class Spinner extends AdapterView<SpinnerAdapter> implements View.OnClickListener {

    private int mSelectedPosition = 0;

    public Spinner(Context context) {
        super(context);
        // 自注册点击监听：宿主快照据此把 Spinner 物化为可点击节点，
        // 点击回灌走 View.performClick -> onClick 循环选中项。
        setOnClickListener(this);
    }

    public Spinner(Context context, AttributeSet attrs) {
        this(context);
    }

    public Spinner(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }

    @Override
    public void setAdapter(SpinnerAdapter adapter) {
        super.setAdapter(adapter);
        this.mSelectedPosition = 0;
        rebuildSelectedView();
    }

    @Override
    public SpinnerAdapter getAdapter() {
        return super.getAdapter();
    }

    private void rebuildSelectedView() {
        super.removeAllViews();
        SpinnerAdapter adapter = getAdapter();
        if (adapter != null && adapter.getCount() > 0) {
            if (this.mSelectedPosition >= adapter.getCount()) {
                this.mSelectedPosition = adapter.getCount() - 1;
            }
            View item = adapter.getView(this.mSelectedPosition, null, this);
            if (item != null) {
                addView(item);
            }
        }
        requestLayout();
    }

    public void setSelection(int position) {
        SpinnerAdapter adapter = getAdapter();
        if (adapter == null) {
            return;
        }
        int count = adapter.getCount();
        if (count == 0) {
            return;
        }
        if (position < 0) {
            position = 0;
        }
        if (position >= count) {
            position = count - 1;
        }
        if (position == this.mSelectedPosition && getChildCount() > 0) {
            return;
        }
        this.mSelectedPosition = position;
        rebuildSelectedView();
        notifyItemSelected();
    }

    public int getSelectedItemPosition() {
        return this.mSelectedPosition;
    }

    @Override
    public View getSelectedView() {
        return getChildCount() > 0 ? getChildAt(0) : null;
    }

    public Object getSelectedItem() {
        SpinnerAdapter adapter = getAdapter();
        return adapter != null && adapter.getCount() > 0
                ? adapter.getItem(this.mSelectedPosition) : null;
    }

    @Override
    public void onClick(View v) {
        SpinnerAdapter adapter = getAdapter();
        if (adapter == null || adapter.getCount() == 0) {
            return;
        }
        int next = (this.mSelectedPosition + 1) % adapter.getCount();
        setSelection(next);
    }

    private void notifyItemSelected() {
        OnItemSelectedListener listener = getOnItemSelectedListener();
        if (listener == null) {
            return;
        }
        SpinnerAdapter adapter = getAdapter();
        listener.onItemSelected(this, getSelectedView(), this.mSelectedPosition,
                adapter != null ? adapter.getItemId(this.mSelectedPosition) : -1);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int width = View.MeasureSpec.getSize(widthMeasureSpec);
        int childW = 0;
        int childH = 0;
        if (getChildCount() > 0) {
            View child = getChildAt(0);
            child.measure(
                    View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.AT_MOST),
                    View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
            childW = child.getMeasuredWidth();
            childH = child.getMeasuredHeight();
        }
        setMeasuredDimension(
                View.resolveSize(childW + getPaddingLeft() + getPaddingRight(), widthMeasureSpec),
                View.resolveSize(childH + getPaddingTop() + getPaddingBottom(), heightMeasureSpec));
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        if (getChildCount() == 0) {
            return;
        }
        View child = getChildAt(0);
        int x = getPaddingLeft();
        int y = getPaddingTop();
        child.layout(x, y, x + child.getMeasuredWidth(), y + child.getMeasuredHeight());
    }
}
