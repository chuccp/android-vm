package android.widget;

/**
 * 列表适配器基（AOSP android.widget.BaseAdapter；§31.7 C 档 lib 骨架）。
 */
public abstract class BaseAdapter implements ListAdapter, SpinnerAdapter {

    private final android.database.DataSetObservable mDataSetObservable =
            new android.database.DataSetObservable();

    public BaseAdapter() {
    }

    public boolean areAllItemsEnabled() {
        return true;
    }

    public boolean isEnabled(int position) {
        return true;
    }

    public android.view.View getDropDownView(int position, android.view.View convertView,
                                             android.view.ViewGroup parent) {
        return getView(position, convertView, parent);
    }

    public int getItemViewType(int position) {
        return 0;
    }

    public int getViewTypeCount() {
        return 1;
    }

    public boolean isEmpty() {
        return getCount() == 0;
    }

    public void notifyDataSetChanged() {
        this.mDataSetObservable.notifyChanged();
    }

    public void notifyDataSetInvalidated() {
        this.mDataSetObservable.notifyInvalidated();
    }

    public void registerDataSetObserver(android.database.DataSetObserver observer) {
        this.mDataSetObservable.registerObserver(observer);
    }

    public void unregisterDataSetObserver(android.database.DataSetObserver observer) {
        this.mDataSetObservable.unregisterObserver(observer);
    }

    public boolean hasStableIds() {
        return false;
    }
}
