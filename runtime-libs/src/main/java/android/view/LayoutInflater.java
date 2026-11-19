package android.view;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 布局填充器（AOSP android.view.LayoutInflater；§31.7 C 档 lib 骨架——视图
 * 构建面归桥接层；本面工厂链/主题态 + inflate 空转/异常显式）。
 */
public abstract class LayoutInflater {

    public interface Factory {
        View onCreateView(String name, Context context, AttributeSet attrs);
    }

    public interface Factory2 extends Factory {
        View onCreateView(View parent, String name, Context context, AttributeSet attrs);
    }

    private final Context mContext;
    private Factory mFactory;
    private Factory2 mFactory2;

    protected LayoutInflater(Context context) {
        this.mContext = context;
    }

    protected LayoutInflater(LayoutInflater original, Context newContext) {
        this.mContext = newContext;
    }

    public static LayoutInflater from(Context context) {
        return (LayoutInflater) context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
    }

    public Context getContext() {
        return this.mContext;
    }

    public LayoutInflater cloneInContext(Context newContext) {
        return this;
    }

    public View inflate(int resource, ViewGroup root) {
        return inflate(resource, root, root != null);
    }

    public View inflate(int resource, ViewGroup root, boolean attachToRoot) {
        throw new UnsupportedOperationException("LayoutInflater.inflate: 视图构建缝归桥接层");
    }

    public View inflate(org.xmlpull.v1.XmlPullParser parser, ViewGroup root) {
        return inflate(parser, root, root != null);
    }

    public View inflate(org.xmlpull.v1.XmlPullParser parser, ViewGroup root,
                        boolean attachToRoot) {
        throw new UnsupportedOperationException("LayoutInflater.inflate: 视图构建缝归桥接层");
    }

    public View createView(String name, String prefix, AttributeSet attrs) {
        return null;
    }

    public void setFactory(Factory factory) {
        if (mFactory != null) {
            throw new IllegalStateException("A factory has already been set on this LayoutInflater");
        }
        mFactory = factory;
    }

    public void setFactory2(Factory2 factory2) {
        if (mFactory != null && mFactory != factory2) {
            throw new IllegalStateException("A factory has already been set on this LayoutInflater");
        }
        mFactory = factory2;
        mFactory2 = factory2;
    }

    public Factory getFactory() {
        return mFactory;
    }

    public Factory2 getFactory2() {
        return mFactory2;
    }
}
