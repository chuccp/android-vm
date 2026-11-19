package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;

/**
 * android.widget.AbsoluteLayout（AOSP 公开面，deprecated）。
 *
 * <p>子 View 以 LayoutParams.x/y 绝对定位。本 VM 的 measure/layout 由宿主
 * 渲染管线（ViewGroup 内建语义）承接，本类先落公开面：构造器 +
 * LayoutParams(x, y) + generate/check 族（Gaiax ZHAbsoluteLayout 超类，
 * 缺类即 super not found → 定义失败级联）。x/y 的绝对定位布局后置。
 */
@Deprecated
public class AbsoluteLayout extends ViewGroup {

    public AbsoluteLayout(Context context) {
        super(context);
    }

    public AbsoluteLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public AbsoluteLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public AbsoluteLayout(Context context, AttributeSet attrs, int defStyleAttr,
                          int defStyleRes) {
        // 宿主 ViewGroup 内建当前面到 3 参构造；4 参回落 3 参（defStyleRes 忽略）。
        super(context, attrs, defStyleAttr);
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        // AOSP AbsoluteLayout 语义：子 View 左上角 = padding + LayoutParams.{x,y}
        final int count = getChildCount();
        final int padLeft = getPaddingLeft();
        final int padTop = getPaddingTop();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == View.GONE) continue;
            int x = padLeft;
            int y = padTop;
            ViewGroup.LayoutParams p = child.getLayoutParams();
            if (p instanceof LayoutParams) {
                x += ((LayoutParams) p).x;
                y += ((LayoutParams) p).y;
            }
            child.layout(x, y, x + child.getMeasuredWidth(), y + child.getMeasuredHeight());
        }
    }

    @Override
    public LayoutParams generateLayoutParams(AttributeSet attrs) {
        return new LayoutParams(getContext(), attrs);
    }

    @Override
    protected LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT, 0, 0);
    }

    @Override
    protected ViewGroup.LayoutParams generateLayoutParams(ViewGroup.LayoutParams p) {
        return new LayoutParams(p);
    }

    @Override
    protected boolean checkLayoutParams(ViewGroup.LayoutParams p) {
        return p instanceof LayoutParams;
    }

    public static class LayoutParams extends ViewGroup.LayoutParams {
        public int x;
        public int y;

        public LayoutParams(int width, int height, int x, int y) {
            super(width, height);
            this.x = x;
            this.y = y;
        }

        public LayoutParams(Context c, AttributeSet attrs) {
            super(c, attrs);
        }

        public LayoutParams(ViewGroup.LayoutParams source) {
            super(source);
        }

        public String debug(String output) {
            return output + "Absolute.LayoutParams={width=" + width + ", height=" + height
                    + " x=" + x + " y=" + y + "}";
        }
    }
}
