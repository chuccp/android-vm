package android.widget;

import android.content.Context;
import android.view.Menu;
import android.view.MenuBuilder;
import android.view.MenuItem;
import android.view.VmLayoutHost;
import android.view.View;

/**
 * 弹出菜单（AOSP android.widget.PopupMenu 公开面；§31.7 C 档 lib——菜单本体
 * 由宿主真 {@code android.widget.PopupMenu} 出画：原生淡入/阴影/外点关闭）。
 *
 * <p>guest 侧维护菜单项集合（{@link MenuBuilder}）与监听器语义；{@link #show()}
 * 经 {@link VmLayoutHost#showPopupMenu} 把锚点/标题镜像/token 交给宿主弹层桥，
 * 宿主点击菜单项经 {@code onHostMenuItemClick(index)} 回灌（派发
 * OnMenuItemClickListener 后按 AOSP 语义关闭），外点关闭经
 * {@code onHostDismissed()} 回灌（派发 OnDismissListener）。
 */
public class PopupMenu {

    public interface OnMenuItemClickListener {
        boolean onMenuItemClick(MenuItem item);
    }

    public interface OnDismissListener {
        void onDismiss(PopupMenu menu);
    }

    private final Context mContext;
    private final View mAnchor;
    private final MenuBuilder mMenu = new MenuBuilder();
    private OnMenuItemClickListener mMenuItemClickListener;
    private OnDismissListener mOnDismissListener;
    private boolean mShowing;

    public PopupMenu(Context context, View anchor) {
        this.mContext = context;
        this.mAnchor = anchor;
    }

    public PopupMenu(Context context, View anchor, int gravity) {
        this(context, anchor);
    }

    public PopupMenu(Context context, View anchor, int gravity, int popupStyleAttr,
                     int popupStyleRes) {
        this(context, anchor);
    }

    public Menu getMenu() {
        return this.mMenu;
    }

    /** XML 菜单膨胀（lib 无资源菜单引擎；宿主侧菜单由 getMenu() 程序化构建）。 */
    public void inflate(int menuRes) {
    }

    public void setOnMenuItemClickListener(OnMenuItemClickListener listener) {
        this.mMenuItemClickListener = listener;
    }

    public void setOnDismissListener(OnDismissListener listener) {
        this.mOnDismissListener = listener;
    }

    /** 展示（宿主真 PopupMenu；重复 show 为空转，同 AOSP）。 */
    public void show() {
        if (mShowing) {
            return;
        }
        mShowing = true;
        VmLayoutHost.showPopupMenu(this, mAnchor, mMenu.mTitleArray);
    }

    /** 关闭（幂等；派发 OnDismissListener）。 */
    public void dismiss() {
        if (!mShowing) {
            return;
        }
        mShowing = false;
        VmLayoutHost.dismissPopupMenu(this);
        if (mOnDismissListener != null) {
            mOnDismissListener.onDismiss(this);
        }
    }

    /** 宿主菜单项点击回灌：派发 OnMenuItemClickListener 后关闭（AOSP 语义）。 */
    public void onHostMenuItemClick(int index) {
        MenuItem item = mMenu.getItem(index);
        if (item != null && mMenuItemClickListener != null) {
            mMenuItemClickListener.onMenuItemClick(item);
        }
        dismiss();
    }

    /** 宿主外点关闭回灌：同步展示态并派发 OnDismissListener。 */
    public void onHostDismissed() {
        if (!mShowing) {
            return;
        }
        mShowing = false;
        if (mOnDismissListener != null) {
            mOnDismissListener.onDismiss(this);
        }
    }
}
