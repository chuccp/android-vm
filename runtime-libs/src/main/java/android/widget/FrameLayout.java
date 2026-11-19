package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;

/**
 * 帧布局（AOSP android.widget.FrameLayout 公开面；§31.7 C 档 lib——子类可覆写；
 * 超类 ViewGroup 为内建，字段布局由 class linker 派生）。子 View 叠放（同原点），
 * measure = 子最大尺寸 + padding。
 */
public class FrameLayout extends ViewGroup {

    private boolean mMeasureAllChildren = false;

    public FrameLayout(Context context) {
        super(context);
    }

    public FrameLayout(Context context, AttributeSet attrs) {
        this(context);
    }

    public FrameLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }

    public void setMeasureAllChildren(boolean measureAll) {
        mMeasureAllChildren = measureAll;
    }

    public boolean getMeasureAllChildren() {
        return mMeasureAllChildren;
    }

    // AOSP FrameLayout LP 工厂（协变返回 FrameLayout.LayoutParams）。
    @Override
    public LayoutParams generateLayoutParams(AttributeSet attrs) {
        return new LayoutParams(getContext(), attrs);
    }

    @Override
    protected LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
    }

    @Override
    protected ViewGroup.LayoutParams generateLayoutParams(ViewGroup.LayoutParams p) {
        return new LayoutParams(p);
    }

    @Override
    protected boolean checkLayoutParams(ViewGroup.LayoutParams p) {
        return p instanceof LayoutParams;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int count = getChildCount();
        int maxW = 0;
        int maxH = 0;
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == View.GONE) continue;
            measureChild(child, widthMeasureSpec, heightMeasureSpec);
            LayoutParams lp = (LayoutParams) child.getLayoutParams();
            int m = lp != null ? lp.leftMargin + lp.rightMargin : 0;
            int mv = lp != null ? lp.topMargin + lp.bottomMargin : 0;
            maxW = Math.max(maxW, child.getMeasuredWidth() + m);
            maxH = Math.max(maxH, child.getMeasuredHeight() + mv);
        }
        int w = maxW + getPaddingLeft() + getPaddingRight();
        int h = maxH + getPaddingTop() + getPaddingBottom();
        setMeasuredDimension(resolveSize(w, widthMeasureSpec), resolveSize(h, heightMeasureSpec));
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        int count = getChildCount();
        int pl = getPaddingLeft();
        int pt = getPaddingTop();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == View.GONE) continue;
            LayoutParams lp = (LayoutParams) child.getLayoutParams();
            int ml = lp != null ? lp.leftMargin : 0;
            int mt = lp != null ? lp.topMargin : 0;
            int cw = child.getMeasuredWidth();
            int ch = child.getMeasuredHeight();
            child.layout(pl + ml, pt + mt, pl + ml + cw, pt + mt + ch);
        }
    }

    /** 布局参数（AOSP FrameLayout.LayoutParams）。 */
    public static class LayoutParams extends ViewGroup.MarginLayoutParams {
        public int gravity = -1;

        public LayoutParams(Context c, AttributeSet attrs) {
            super(c, attrs);
        }

        public LayoutParams(int width, int height) {
            super(width, height);
        }

        public LayoutParams(int width, int height, int gravity) {
            super(width, height);
            this.gravity = gravity;
        }

        public LayoutParams(ViewGroup.LayoutParams source) {
            super(source);
        }

        public LayoutParams(ViewGroup.MarginLayoutParams source) {
            super(source);
        }

        public LayoutParams(LayoutParams source) {
            super(source);
            this.gravity = source.gravity;
        }
    }
}
