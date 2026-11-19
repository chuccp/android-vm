package android.view;

import android.content.Context;
import android.util.AttributeSet;

/**
 * android.view.ViewStub（AOSP 公开面）。
 *
 * <p>懒加载占位 View；真实 inflate 由布局桥接/宿主物化负责。当前保留
 * 构造、layout/inflatedId 与 inflate 接口，避免 material 控件构造期
 * NoClassDefFound（ZHFloatAdFloatView 等）。
 */
public final class ViewStub extends View {

    public interface OnInflateListener {
        void onInflate(ViewStub stub, View inflated);
    }

    private int mInflatedId;
    private int mLayoutResource;
    private OnInflateListener mInflateListener;

    public ViewStub(Context context) {
        super(context);
    }

    public ViewStub(Context context, int layoutResource) {
        super(context);
        mLayoutResource = layoutResource;
    }

    public ViewStub(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public ViewStub(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public int getInflatedId() {
        return mInflatedId;
    }

    public void setInflatedId(int inflatedId) {
        mInflatedId = inflatedId;
    }

    public int getLayoutResource() {
        return mLayoutResource;
    }

    public void setLayoutResource(int layoutResource) {
        mLayoutResource = layoutResource;
    }

    public void setOnInflateListener(OnInflateListener listener) {
        mInflateListener = listener;
    }

    @Override
    public void setVisibility(int visibility) {
        super.setVisibility(visibility);
    }

    public View inflate() {
        return this;
    }
}
