package android.widget;

import android.view.View;
import android.view.ViewGroup;

/**
 * 适配器接口（AOSP android.widget.Adapter；§31.7 C 档 lib 接口面）。
 */
public interface Adapter {

    int getItemViewType(int position);

    int getViewTypeCount();

    boolean isEmpty();

    void registerDataSetObserver(android.database.DataSetObserver observer);

    void unregisterDataSetObserver(android.database.DataSetObserver observer);

    int getCount();

    Object getItem(int position);

    long getItemId(int position);

    boolean hasStableIds();

    View getView(int position, View convertView, ViewGroup parent);

    static final int IGNORE_ITEM_VIEW_TYPE = -1;
}
