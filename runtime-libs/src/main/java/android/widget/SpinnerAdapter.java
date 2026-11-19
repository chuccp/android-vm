package android.widget;

import android.view.View;
import android.view.ViewGroup;

/**
 * 下拉适配器接口（AOSP android.widget.SpinnerAdapter；§31.7 C 档 lib 接口面）。
 */
public interface SpinnerAdapter extends Adapter {

    View getDropDownView(int position, View convertView, ViewGroup parent);
}
