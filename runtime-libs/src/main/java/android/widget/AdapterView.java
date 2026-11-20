package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;

/**
 * 适配器视图基（AOSP android.widget.AdapterView 公开面；§31.7 C 档 lib 骨架——
 * 适配器绑定态 + 选择态；item 渲染归桥接层）。
 */
public abstract class AdapterView<T extends Adapter> extends ViewGroup {

    public interface OnItemClickListener {
        void onItemClick(AdapterView<?> parent, View view, int position, long id);
    }

    public interface OnItemLongClickListener {
        boolean onItemLongClick(AdapterView<?> parent, View view, int position, long id);
    }

    public interface OnItemSelectedListener {
        void onItemSelected(AdapterView<?> parent, View view, int position, long id);

        void onNothingSelected(AdapterView<?> parent);
    }

    public static final int ITEM_VIEW_TYPE_IGNORE = -1;
    public static final int ITEM_VIEW_TYPE_HEADER_OR_FOOTER = -2;
    public static final int SYNC_SELECTED_POSITION = 0;
    public static final int SYNC_FIRST_POSITION = 1;

    int mFirstPosition = 0;
    int mNextSelectedPosition = -1;
    long mNextSelectedRowId = -1;
    int mSelectedPosition = -1;
    long mSelectedRowId = -1;
    T mAdapter;
    private OnItemClickListener mOnItemClickListener;
    private OnItemSelectedListener mOnItemSelectedListener;

    public AdapterView(Context context) {
        super(context);
    }

    public AdapterView(Context context, AttributeSet attrs) {
        this(context);
    }

    public T getAdapter() {
        return this.mAdapter;
    }

    public void setAdapter(T adapter) {
        this.mAdapter = adapter;
    }

    // AOSP 语义：adapter 视图的子项由 adapter 驱动，addView 直接抛异常。VM 下
    // ListView/Spinner 的实现需要在 lib 层物化 item 子 View（容器 addView 是
    // ViewGroup 内建 intrinsic），因此这里放开为 super 直通；guest 自行 addView
    // 的兼容面也因此变宽（不再是硬错误）。
    public void addView(View child) {
        super.addView(child);
    }

    public void addView(View child, int index) {
        super.addView(child, index);
    }

    public void addView(View child, ViewGroup.LayoutParams params) {
        super.addView(child, params);
    }

    public void removeView(View child) {
        super.removeView(child);
    }

    public void removeAllViews() {
        super.removeAllViews();
    }

    public View getSelectedView() {
        return null;
    }

    public Object getSelectedItem() {
        return this.mAdapter != null && this.mSelectedPosition >= 0
                ? this.mAdapter.getItem(this.mSelectedPosition) : null;
    }

    public long getSelectedItemId() {
        return this.mSelectedRowId;
    }

    public int getSelectedItemPosition() {
        return this.mSelectedPosition;
    }

    public int getSelectedItemViewType() {
        return 0;
    }

    public int getCount() {
        return this.mAdapter != null ? this.mAdapter.getCount() : 0;
    }

    public int getFirstVisiblePosition() {
        return this.mFirstPosition;
    }

    public int getLastVisiblePosition() {
        return this.mFirstPosition;
    }

    public int getPositionForView(View view) {
        return -1;
    }

    public void setSelection(int position) {
    }

    public void setEmptyView(View emptyView) {
    }

    public View getEmptyView() {
        return null;
    }

    public void setOnItemClickListener(OnItemClickListener listener) {
        this.mOnItemClickListener = listener;
    }

    public final OnItemClickListener getOnItemClickListener() {
        return this.mOnItemClickListener;
    }

    public void setOnItemLongClickListener(OnItemLongClickListener listener) {
    }

    public void setOnItemSelectedListener(OnItemSelectedListener listener) {
        this.mOnItemSelectedListener = listener;
    }

    public final OnItemSelectedListener getOnItemSelectedListener() {
        return this.mOnItemSelectedListener;
    }

    public Object getItemAtPosition(int position) {
        return this.mAdapter != null ? this.mAdapter.getItem(position) : null;
    }

    public long getItemIdAtPosition(int position) {
        return this.mAdapter != null ? this.mAdapter.getItemId(position) : -1;
    }
}
