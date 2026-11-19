package android.view;

import android.graphics.drawable.Drawable;

/**
 * 子菜单接口（AOSP android.view.SubMenu；§31.7 C 档 lib 接口面）。
 */
public interface SubMenu extends Menu {
    SubMenu setHeaderTitle(int titleRes);

    SubMenu setHeaderTitle(CharSequence title);

    SubMenu setHeaderIcon(int iconRes);

    SubMenu setHeaderIcon(Drawable icon);

    SubMenu setHeaderView(View view);

    void clearHeader();

    SubMenu setIcon(int iconRes);

    SubMenu setIcon(Drawable icon);

    MenuItem getItem();
}
