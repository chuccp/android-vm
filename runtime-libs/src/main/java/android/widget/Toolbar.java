package android.widget;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.view.Menu;
import android.view.MenuBuilder;
import android.view.MenuItem;
import android.view.View;

/**
 * 工具栏（AOSP android.widget.Toolbar 公开面；§31.7 C 档 lib——宿主物化层把
 * 同一节点换成宿主真 android.widget.Toolbar 出画）。
 *
 * <p>guest 侧维护 title/subtitle/菜单/导航点击语义；宿主真 Toolbar 的标题、
 * 子标题、导航按钮（汉堡）与菜单项由快照属性重建，菜单项点击经
 * onHostMenuItemClick 回灌，导航按钮点击复用点击通道（performClick 覆写派发
 * 到 navigation listener）。
 */
public class Toolbar extends FrameLayout {

    public interface OnMenuItemClickListener {
        boolean onMenuItemClick(MenuItem item);
    }

    private CharSequence mTitle;
    private CharSequence mSubtitle;
    private View.OnClickListener mNavigationClickListener;
    private MenuBuilder mMenu;
    private OnMenuItemClickListener mOnMenuItemClickListener;

    public Toolbar(Context context) {
        super(context);
    }

    public Toolbar(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public Toolbar(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public Toolbar(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr);
    }

    public void setTitle(CharSequence title) {
        this.mTitle = title;
    }

    public void setTitle(int resId) {
        this.mTitle = null;
    }

    public CharSequence getTitle() {
        return this.mTitle;
    }

    public void setSubtitle(CharSequence subtitle) {
        this.mSubtitle = subtitle;
    }

    public void setSubtitle(int resId) {
        this.mSubtitle = null;
    }

    public CharSequence getSubtitle() {
        return this.mSubtitle;
    }

    public void setNavigationOnClickListener(View.OnClickListener listener) {
        this.mNavigationClickListener = listener;
    }

    public void setNavigationIcon(int resId) {
    }

    public void setNavigationIcon(Drawable icon) {
    }

    public void setNavigationContentDescription(CharSequence description) {
    }

    public void setNavigationContentDescription(int resId) {
    }

    public void setLogo(int resId) {
    }

    public void setLogo(Drawable drawable) {
    }

    public void setTitleTextColor(int color) {
    }

    public void setSubtitleTextColor(int color) {
    }

    /** 菜单容器（AOSP 程序化建菜单面；宿主物化层按快照重建真菜单项）。 */
    public Menu getMenu() {
        if (mMenu == null) {
            mMenu = new MenuBuilder();
        }
        return mMenu;
    }

    public void inflateMenu(int resId) {
    }

    public void setOnMenuItemClickListener(OnMenuItemClickListener listener) {
        this.mOnMenuItemClickListener = listener;
    }

    public OnMenuItemClickListener getOnMenuItemClickListener() {
        return this.mOnMenuItemClickListener;
    }

    public void setOverflowIcon(Drawable icon) {
    }

    public Drawable getOverflowIcon() {
        return null;
    }

    /** 宿主菜单项点击回灌（UI 桥 intrinsic 调用，非 AOSP 公开面）。 */
    public void onHostMenuItemClick(int index) {
        if (mMenu == null) {
            return;
        }
        MenuItem item = mMenu.getItem(index);
        if (item != null && mOnMenuItemClickListener != null) {
            mOnMenuItemClickListener.onMenuItemClick(item);
        }
    }

    /** 导航按钮（宿主汉堡图标）点击经点击通道回到这里派发。 */
    @Override
    public boolean performClick() {
        if (mNavigationClickListener != null) {
            mNavigationClickListener.onClick(this);
            return true;
        }
        return super.performClick();
    }
}
