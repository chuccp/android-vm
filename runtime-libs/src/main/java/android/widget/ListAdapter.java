package android.widget;

/**
 * 列表适配器接口（AOSP android.widget.ListAdapter；§31.7 C 档 lib 接口面）。
 */
public interface ListAdapter extends Adapter {

    boolean areAllItemsEnabled();

    boolean isEnabled(int position);
}
