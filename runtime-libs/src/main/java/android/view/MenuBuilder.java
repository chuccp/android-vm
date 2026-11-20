package android.view;

import android.content.ComponentName;
import android.content.Intent;
import android.graphics.drawable.Drawable;

import java.util.ArrayList;

/**
 * 菜单实现（AOSP MenuBuilder 语义面；§31.7 C 档 lib 实现）。
 *
 * <p>Toolbar/PopupMenu 共用：菜单项集合 + 标题镜像（mTitleArray，UI 桥快照/
 * 宿主桥直接读该字段重建宿主原生菜单；避免在协议层遍历 VM 容器对象）。
 */
public class MenuBuilder implements Menu {

    private final ArrayList<Item> mItems = new ArrayList<>();
    private final ArrayList<MenuCallback> mCallbacks = new ArrayList<>();
    private int mNextId = 1;

    /** 当前菜单项标题镜像（每次增删/改标题后重建；快照侧按字段名读取）。 */
    public String[] mTitleArray = new String[0];

    @Override
    public MenuItem add(CharSequence title) {
        return add(0, mNextId++, 0, title);
    }

    @Override
    public MenuItem add(int titleRes) {
        return add(0, mNextId++, 0, "");
    }

    @Override
    public MenuItem add(int groupId, int itemId, int order, CharSequence title) {
        Item item = new Item(this, groupId, itemId, order, title);
        mItems.add(item);
        syncTitles();
        return item;
    }

    @Override
    public MenuItem add(int groupId, int itemId, int order, int titleRes) {
        return add(groupId, itemId, order, "");
    }

    @Override
    public int addIntentOptions(int groupId, int itemId, int order, ComponentName caller,
                                Intent[] specifics, Intent intent, int flags,
                                MenuItem[] outSpecificItems) {
        return 0;
    }

    @Override
    public void removeGroup(int groupId) {
        for (int i = mItems.size() - 1; i >= 0; i--) {
            if (mItems.get(i).getGroupId() == groupId) {
                mItems.remove(i);
            }
        }
        syncTitles();
    }

    @Override
    public void remove(int id) {
        for (int i = mItems.size() - 1; i >= 0; i--) {
            if (mItems.get(i).getItemId() == id) {
                mItems.remove(i);
            }
        }
        syncTitles();
    }

    @Override
    public void clear() {
        mItems.clear();
        syncTitles();
    }

    @Override
    public void setGroupCheckable(int group, boolean checkable, boolean exclusive) {
        for (Item item : mItems) {
            if (item.getGroupId() == group) {
                item.setCheckable(checkable);
            }
        }
    }

    @Override
    public void setGroupVisible(int group, boolean visible) {
        for (Item item : mItems) {
            if (item.getGroupId() == group) {
                item.setVisible(visible);
            }
        }
    }

    @Override
    public void setGroupEnabled(int group, boolean enabled) {
        for (Item item : mItems) {
            if (item.getGroupId() == group) {
                item.setEnabled(enabled);
            }
        }
    }

    @Override
    public boolean hasVisibleItems() {
        for (Item item : mItems) {
            if (item.isVisible()) {
                return true;
            }
        }
        return false;
    }

    @Override
    public MenuItem findItem(int id) {
        for (Item item : mItems) {
            if (item.getItemId() == id) {
                return item;
            }
        }
        return null;
    }

    @Override
    public int size() {
        return mItems.size();
    }

    @Override
    public MenuItem getItem(int index) {
        return index >= 0 && index < mItems.size() ? mItems.get(index) : null;
    }

    @Override
    public void close() {
    }

    @Override
    public boolean performShortcut(int keyCode, KeyEvent event, int flags) {
        return false;
    }

    @Override
    public boolean isShortcutKey(int keyCode, KeyEvent event) {
        return false;
    }

    @Override
    public boolean performIdentifierAction(int id, int flags) {
        return false;
    }

    @Override
    public void removeMenuCallback(MenuCallback cb) {
        if (cb != null) {
            mCallbacks.remove(cb);
        }
    }

    @Override
    public void addMenuCallback(MenuCallback cb) {
        if (cb != null) {
            mCallbacks.add(cb);
        }
    }

    private void syncTitles() {
        String[] titles = new String[mItems.size()];
        for (int i = 0; i < mItems.size(); i++) {
            titles[i] = mItems.get(i).getTitleString();
        }
        mTitleArray = titles;
    }

    /** 菜单项实现（AOSP MenuItem 语义面）。 */
    public static final class Item implements MenuItem {
        private final MenuBuilder mOwner;
        private final int mGroupId;
        private final int mItemId;
        private final int mOrder;
        private String mTitle;
        private String mTitleCondensed;
        private Drawable mIcon;
        private Intent mIntent;
        private char mNumericShortcut;
        private char mAlphabeticShortcut;
        private boolean mCheckable;
        private boolean mChecked;
        private boolean mVisible = true;
        private boolean mEnabled = true;
        private int mShowAsAction;
        private View mActionView;
        private ActionProvider mActionProvider;
        private OnMenuItemClickListener mClickListener;
        private OnActionExpandListener mExpandListener;

        Item(MenuBuilder owner, int groupId, int itemId, int order, CharSequence title) {
            this.mOwner = owner;
            this.mGroupId = groupId;
            this.mItemId = itemId;
            this.mOrder = order;
            this.mTitle = title == null ? "" : title.toString();
        }

        String getTitleString() {
            return mTitle;
        }

        @Override
        public int getItemId() {
            return mItemId;
        }

        @Override
        public int getGroupId() {
            return mGroupId;
        }

        @Override
        public int getOrder() {
            return mOrder;
        }

        @Override
        public MenuItem setTitle(CharSequence title) {
            mTitle = title == null ? "" : title.toString();
            mOwner.syncTitles();
            return this;
        }

        @Override
        public MenuItem setTitle(int titleRes) {
            return this;
        }

        @Override
        public CharSequence getTitle() {
            return mTitle;
        }

        @Override
        public MenuItem setTitleCondensed(CharSequence title) {
            mTitleCondensed = title == null ? null : title.toString();
            return this;
        }

        @Override
        public CharSequence getTitleCondensed() {
            return mTitleCondensed != null ? mTitleCondensed : mTitle;
        }

        @Override
        public MenuItem setIcon(Drawable icon) {
            mIcon = icon;
            return this;
        }

        @Override
        public MenuItem setIcon(int iconRes) {
            return this;
        }

        @Override
        public Drawable getIcon() {
            return mIcon;
        }

        @Override
        public MenuItem setIntent(Intent intent) {
            mIntent = intent;
            return this;
        }

        @Override
        public Intent getIntent() {
            return mIntent;
        }

        @Override
        public MenuItem setShortcut(char numericChar, char alphaChar) {
            mNumericShortcut = numericChar;
            mAlphabeticShortcut = alphaChar;
            return this;
        }

        @Override
        public MenuItem setNumericShortcut(char numericChar) {
            mNumericShortcut = numericChar;
            return this;
        }

        @Override
        public char getNumericShortcut() {
            return mNumericShortcut;
        }

        @Override
        public MenuItem setAlphabeticShortcut(char alphaChar) {
            mAlphabeticShortcut = alphaChar;
            return this;
        }

        @Override
        public char getAlphabeticShortcut() {
            return mAlphabeticShortcut;
        }

        @Override
        public MenuItem setCheckable(boolean checkable) {
            mCheckable = checkable;
            return this;
        }

        @Override
        public boolean isCheckable() {
            return mCheckable;
        }

        @Override
        public MenuItem setChecked(boolean checked) {
            mChecked = checked;
            return this;
        }

        @Override
        public boolean isChecked() {
            return mChecked;
        }

        @Override
        public MenuItem setVisible(boolean visible) {
            mVisible = visible;
            return this;
        }

        @Override
        public boolean isVisible() {
            return mVisible;
        }

        @Override
        public MenuItem setEnabled(boolean enabled) {
            mEnabled = enabled;
            return this;
        }

        @Override
        public boolean isEnabled() {
            return mEnabled;
        }

        @Override
        public boolean hasSubMenu() {
            return false;
        }

        @Override
        public SubMenu getSubMenu() {
            return null;
        }

        @Override
        public MenuItem setOnMenuItemClickListener(OnMenuItemClickListener listener) {
            mClickListener = listener;
            return this;
        }

        @Override
        public ContextMenu.ContextMenuInfo getMenuInfo() {
            return null;
        }

        @Override
        public void setShowAsAction(int actionEnum) {
            mShowAsAction = actionEnum;
        }

        @Override
        public MenuItem setShowAsActionFlags(int actionEnum) {
            mShowAsAction = actionEnum;
            return this;
        }

        @Override
        public MenuItem setActionView(View view) {
            mActionView = view;
            return this;
        }

        @Override
        public MenuItem setActionView(int resId) {
            return this;
        }

        @Override
        public View getActionView() {
            return mActionView;
        }

        @Override
        public MenuItem setActionProvider(ActionProvider actionProvider) {
            mActionProvider = actionProvider;
            return this;
        }

        @Override
        public ActionProvider getActionProvider() {
            return mActionProvider;
        }

        @Override
        public boolean expandActionView() {
            return mActionView != null;
        }

        @Override
        public boolean collapseActionView() {
            return false;
        }

        @Override
        public boolean isActionViewExpanded() {
            return false;
        }

        @Override
        public MenuItem setOnActionExpandListener(OnActionExpandListener listener) {
            mExpandListener = listener;
            return this;
        }
    }
}
