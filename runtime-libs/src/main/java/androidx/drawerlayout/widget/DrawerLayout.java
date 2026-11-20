package androidx.drawerlayout.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import java.util.ArrayList;
import java.util.List;

/**
 * 侧滑菜单（androidx.drawerlayout.widget.DrawerLayout 公开面；VM lib 实现）。
 *
 * <p>VM 单窗口模型：真正的抽屉手势/动画/遮罩由宿主物化层用真 DrawerLayout
 * 承担（宿主 addView 结构 + openDrawer/closeDrawer 状态同步）；guest 侧维护
 * 打开态与 DrawerListener 语义，布局以 FrameLayout 叠放近似（内容 + 抽屉子项）。
 * 打开态经快照 kPropDrawerOpen 同步宿主，宿主侧 gesture 开关经
 * onHostDrawerStateChanged 回灌。
 */
public class DrawerLayout extends FrameLayout {

    public static final int LOCK_MODE_UNLOCKED = 0;
    public static final int LOCK_MODE_LOCKED_CLOSED = 1;
    public static final int LOCK_MODE_LOCKED_OPEN = 2;

    public static final int STATE_IDLE = 0;
    public static final int STATE_DRAGGING = 1;
    public static final int STATE_SETTLING = 2;

    // 快照 prop 读取字段（名固定）。
    private boolean mDrawerOpen;
    private int mLockMode = LOCK_MODE_UNLOCKED;
    private int mScrimColor = 0x99000000;
    private float mDrawerElevation;
    private final List<DrawerListener> mListeners = new ArrayList<>();

    public DrawerLayout(Context context) {
        super(context);
    }

    public DrawerLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public DrawerLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void openDrawer(int gravity) {
        setOpenState(true);
    }

    public void openDrawer(View drawerView) {
        setOpenState(true);
    }

    public void closeDrawer(int gravity) {
        setOpenState(false);
    }

    public void closeDrawer(View drawerView) {
        setOpenState(false);
    }

    public boolean isDrawerOpen(int drawerGravity) {
        return this.mDrawerOpen;
    }

    public boolean isDrawerOpen(View drawer) {
        return this.mDrawerOpen;
    }

    public boolean isDrawerVisible(int drawerGravity) {
        return this.mDrawerOpen;
    }

    public boolean isDrawerVisible(View drawer) {
        return this.mDrawerOpen;
    }

    public void setDrawerLockMode(int lockMode) {
        this.mLockMode = lockMode;
    }

    public void setDrawerLockMode(int lockMode, int edgeGravity) {
        this.mLockMode = lockMode;
    }

    public void setDrawerLockMode(int lockMode, View drawerView) {
        this.mLockMode = lockMode;
    }

    public int getDrawerLockMode(int edgeGravity) {
        return this.mLockMode;
    }

    public int getDrawerLockMode(View drawerView) {
        return this.mLockMode;
    }

    public void setScrimColor(int color) {
        this.mScrimColor = color;
    }

    public int getScrimColor() {
        return this.mScrimColor;
    }

    public void setDrawerElevation(float elevation) {
        this.mDrawerElevation = elevation;
    }

    public float getDrawerElevation() {
        return this.mDrawerElevation;
    }

    public void setStatusBarBackgroundColor(int color, boolean animate) {
    }

    public void setStatusBarBackground(int resId) {
    }

    public void setDrawerTitle(int edgeGravity, CharSequence title) {
    }

    public CharSequence getDrawerTitle(int edgeGravity) {
        return null;
    }

    public void addDrawerListener(DrawerListener listener) {
        if (listener != null) {
            mListeners.add(listener);
        }
    }

    public void removeDrawerListener(DrawerListener listener) {
        if (listener != null) {
            mListeners.remove(listener);
        }
    }

    @Deprecated
    public void setDrawerListener(DrawerListener listener) {
        if (listener != null) {
            mListeners.add(listener);
        }
    }

    /** 宿主 DrawerLayout 状态回灌（UI 桥 intrinsic 调用，非 AOSP 公开面）。 */
    public void onHostDrawerStateChanged(boolean open) {
        setOpenState(open);
    }

    private void setOpenState(boolean open) {
        if (this.mDrawerOpen == open) {
            return;
        }
        this.mDrawerOpen = open;
        List<DrawerListener> copy = new ArrayList<>(mListeners);
        for (DrawerListener listener : copy) {
            if (open) {
                listener.onDrawerOpened(this);
            } else {
                listener.onDrawerClosed(this);
            }
            listener.onDrawerStateChanged(STATE_IDLE);
        }
        requestLayout();
    }

    public interface DrawerListener {
        void onDrawerSlide(View drawerView, float slideOffset);

        void onDrawerOpened(View drawerView);

        void onDrawerClosed(View drawerView);

        void onDrawerStateChanged(int newState);
    }

    /** 空实现基类（AOSP SimpleDrawerListener 同形）。 */
    public static class SimpleDrawerListener implements DrawerListener {
        @Override
        public void onDrawerSlide(View drawerView, float slideOffset) {
        }

        @Override
        public void onDrawerOpened(View drawerView) {
        }

        @Override
        public void onDrawerClosed(View drawerView) {
        }

        @Override
        public void onDrawerStateChanged(int newState) {
        }
    }

    /** 抽屉布局参数（宽高 + gravity；host 物化按 gravity/子项次序识别抽屉）。 */
    public static class LayoutParams extends FrameLayout.LayoutParams {
        public LayoutParams(int width, int height) {
            super(width, height);
        }

        public LayoutParams(int width, int height, int gravity) {
            super(width, height, gravity);
        }

        public LayoutParams(Context c, AttributeSet attrs) {
            super(c, attrs);
        }

        public LayoutParams(ViewGroup.LayoutParams source) {
            super(source);
        }

        public LayoutParams(ViewGroup.MarginLayoutParams source) {
            super(source);
        }
    }

    @Override
    public LayoutParams generateLayoutParams(AttributeSet attrs) {
        return new LayoutParams(getContext(), attrs);
    }

    @Override
    protected LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
    }

    @Override
    protected LayoutParams generateLayoutParams(ViewGroup.LayoutParams p) {
        return p instanceof LayoutParams ? (LayoutParams) p : new LayoutParams(p);
    }

    @Override
    protected boolean checkLayoutParams(ViewGroup.LayoutParams p) {
        return p instanceof LayoutParams;
    }
}
