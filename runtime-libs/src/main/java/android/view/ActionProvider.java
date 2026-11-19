package android.view;

/**
 * 动作提供器（AOSP android.view.ActionProvider；§31.7 C 档 lib 骨架）。
 */
public abstract class ActionProvider {

    public interface VisibilityListener {
        void onActionProviderVisibilityChanged(boolean isVisible);
    }

    private final android.content.Context mContext;

    public ActionProvider(android.content.Context context) {
        this.mContext = context;
    }

    public android.content.Context getContext() {
        return this.mContext;
    }

    public abstract View onCreateActionView();

    public View onCreateActionView(MenuItem forItem) {
        return onCreateActionView();
    }

    public boolean overridesItemVisibility() {
        return false;
    }

    public boolean isVisible() {
        return true;
    }

    public void refreshVisibility() {
    }

    public boolean onPerformDefaultAction() {
        return false;
    }

    public boolean hasSubMenu() {
        return false;
    }

    public void onPrepareSubMenu(SubMenu subMenu) {
    }

    public void setVisibilityListener(VisibilityListener listener) {
    }

    public void reset() {
    }
}
