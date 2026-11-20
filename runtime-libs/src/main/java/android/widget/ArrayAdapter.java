package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;

/**
 * 数组适配器（AOSP android.widget.ArrayAdapter 公开面；§31.7 C 档 lib）。
 *
 * VM 面：item 视图不走 layout/mlayout 资源膨胀（framework 资源表不含
 * android.R.layout.*），统一程序化 TextView（文本 = item.toString()）——
 * ListView/Spinner 的文本行兼容面。资源布局定制留待后续。
 */
public class ArrayAdapter<T> extends BaseAdapter {

    private final Context mContext;
    private final List<T> mObjects = new ArrayList<T>();
    private final int mResource;

    public ArrayAdapter(Context context, int resource) {
        this.mContext = context;
        this.mResource = resource;
    }

    public ArrayAdapter(Context context, int resource, int textViewResourceId) {
        this(context, resource);
    }

    public ArrayAdapter(Context context, int resource, T[] objects) {
        this(context, resource);
        addAll(objects);
    }

    public ArrayAdapter(Context context, int resource, int textViewResourceId, T[] objects) {
        this(context, resource, objects);
    }

    public ArrayAdapter(Context context, int resource, List<T> objects) {
        this(context, resource);
        addAll(objects);
    }

    public ArrayAdapter(Context context, int resource, int textViewResourceId, List<T> objects) {
        this(context, resource, objects);
    }

    public void add(T object) {
        this.mObjects.add(object);
    }

    public void addAll(T... items) {
        if (items != null) {
            this.mObjects.addAll(Arrays.asList(items));
        }
    }

    public void addAll(Collection<? extends T> collection) {
        if (collection != null) {
            this.mObjects.addAll(collection);
        }
    }

    public void insert(T object, int index) {
        this.mObjects.add(index, object);
    }

    public void remove(T object) {
        this.mObjects.remove(object);
    }

    public void clear() {
        this.mObjects.clear();
    }

    @Override
    public int getCount() {
        return this.mObjects.size();
    }

    @Override
    public T getItem(int position) {
        return this.mObjects.get(position);
    }

    @Override
    public long getItemId(int position) {
        return position;
    }

    @Override
    public View getView(int position, View convertView, ViewGroup parent) {
        TextView view = convertView instanceof TextView
                ? (TextView) convertView : new TextView(this.mContext);
        T item = getItem(position);
        view.setText(item == null ? "" : item.toString());
        view.setPadding(16, 16, 16, 16);
        return view;
    }
}
