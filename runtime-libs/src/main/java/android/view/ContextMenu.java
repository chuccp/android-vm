package android.view;

import android.graphics.drawable.Drawable;

/**
 * 上下文菜单接口（AOSP android.view.ContextMenu；§31.7 C 档 lib 接口面）。
 */
public interface ContextMenu extends Menu {

    /** 菜单信息（AOSP ContextMenu.ContextMenuInfo 标记接口）。 */
    interface ContextMenuInfo {
    }

    ContextMenu setHeaderTitle(CharSequence title);

    ContextMenu setHeaderTitle(int titleRes);

    ContextMenu setHeaderIcon(Drawable icon);

    ContextMenu setHeaderIcon(int iconRes);

    ContextMenu setHeaderView(View view);

    void clearHeader();
}
