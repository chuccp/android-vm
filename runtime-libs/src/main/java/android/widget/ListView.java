package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

/**
 * 列表视图（AOSP android.widget.ListView 公开面；§31.7 C 档 lib 骨架）。
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

    public void setAdapter(ListAdapter adapter) {
        super.setAdapter(adapter);
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
