package android.view;

import android.content.Intent;

/**
 * 菜单接口（AOSP android.view.Menu；§31.7 C 档 lib 接口面）。
 */
public interface Menu {
    int USER_MASK = 0x0000ffff;
    int USER_SHIFT = 0;
    int CATEGORY_MASK = 0xffff0000;
    int CATEGORY_SHIFT = 16;

    int NONE = 0;
    int FIRST = 1;
    int CATEGORY_CONTAINER = 0x00010000;
    int CATEGORY_SYSTEM = 0x00020000;
    int CATEGORY_SECONDARY = 0x00030000;
    int CATEGORY_ALTERNATIVE = 0x00040000;

    int FLAG_PERFORM_NO_CLOSE = 0x00000001;
    int FLAG_ALWAYS_PERFORM_CLOSE = 0x00000002;

    MenuItem add(CharSequence title);

    MenuItem add(int titleRes);

    MenuItem add(int groupId, int itemId, int order, CharSequence title);

    MenuItem add(int groupId, int itemId, int order, int titleRes);

    int addIntentOptions(int groupId, int itemId, int order, android.content.ComponentName caller,
                         Intent[] specifics, Intent intent, int flags, MenuItem[] outSpecificItems);

    void removeGroup(int groupId);

    void remove(int id);

    void clear();

    void setGroupCheckable(int group, boolean checkable, boolean exclusive);

    void setGroupVisible(int group, boolean visible);

    void setGroupEnabled(int group, boolean enabled);

    boolean hasVisibleItems();

    MenuItem findItem(int id);

    int size();

    MenuItem getItem(int index);

    void close();

    boolean performShortcut(int keyCode, KeyEvent event, int flags);

    boolean isShortcutKey(int keyCode, KeyEvent event);

    boolean performIdentifierAction(int id, int flags);

    void removeMenuCallback(MenuCallback cb);

    void addMenuCallback(MenuCallback cb);

    /** 菜单回调（AOSP 内部面；空实现）。 */
    interface MenuCallback {
        boolean onMenuItemSelected(Menu menu, MenuItem item);
    }
}
