package android.view;

import android.graphics.drawable.Drawable;

/**
 * 动作模式（AOSP android.view.ActionMode；§31.7 C 档 lib 骨架）。
 */
public abstract class ActionMode {

    /** 回调（AOSP ActionMode.Callback）。 */
    public interface Callback {
        boolean onCreateActionMode(ActionMode mode, Menu menu);

        boolean onPrepareActionMode(ActionMode mode, Menu menu);

        boolean onActionItemClicked(ActionMode mode, MenuItem item);

        void onDestroyActionMode(ActionMode mode);
    }

    public static final int TYPE_PRIMARY = 0;
    public static final int TYPE_FLOATING = 1;

    private Object mTag;
    private boolean mTitleOptionalHint;

    public ActionMode() {
    }

    public void setTitle(CharSequence title) {
    }

    public void setTitle(int resId) {
    }

    public void setSubtitle(CharSequence subtitle) {
    }

    public void setSubtitle(int resId) {
    }

    public void setCustomView(View view) {
    }

    public void invalidate() {
    }

    public void finish() {
    }

    public Menu getMenu() {
        return null;
    }

    public CharSequence getTitle() {
        return null;
    }

    public CharSequence getSubtitle() {
        return null;
    }

    public View getCustomView() {
        return null;
    }

    public void setMenu(Menu menu) {
    }

    public android.view.MenuInflater getMenuInflater() {
        return null;
    }

    public boolean isTitleOptional() {
        return this.mTitleOptionalHint;
    }

    public void setTitleOptionalHint(boolean titleOptional) {
        this.mTitleOptionalHint = titleOptional;
    }

    public void setTag(Object tag) {
        this.mTag = tag;
    }

    public Object getTag() {
        return this.mTag;
    }

    public void setHomeAsUpIndicator(Drawable indicator) {
    }

    public void setHomeAsUpIndicator(int resId) {
    }

    public boolean isUiFocusable() {
        return true;
    }

}
