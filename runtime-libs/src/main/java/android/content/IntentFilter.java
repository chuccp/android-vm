package android.content;

import java.util.ArrayList;

/**
 * 广播过滤器（AOSP android.content.IntentFilter 公开面；§31.7 C 档 lib——纯
 * 数据/逻辑）。VM 无广播派发源：注册/匹配面按数据语义可用。
 */
public class IntentFilter {
    private final ArrayList<String> mActions = new ArrayList<>();
    private final ArrayList<String> mCategories = new ArrayList<>();
    private int mPriority;
    private String mDataScheme;

    public IntentFilter() {
    }

    public IntentFilter(String action) {
        addAction(action);
    }

    public IntentFilter(IntentFilter source) {
        if (source != null) {
            mActions.addAll(source.mActions);
            mCategories.addAll(source.mCategories);
            mPriority = source.mPriority;
            mDataScheme = source.mDataScheme;
        }
    }

    public final void setPriority(int priority) {
        this.mPriority = priority;
    }

    public final int getPriority() {
        return this.mPriority;
    }

    public final void addAction(String action) {
        if (action != null) this.mActions.add(action);
    }

    public final void addCategory(String category) {
        if (category != null) this.mCategories.add(category);
    }

    public final void addDataScheme(String scheme) {
        this.mDataScheme = scheme;
    }

    public final int countActions() {
        return mActions.size();
    }

    public final String getAction(int index) {
        return index >= 0 && index < mActions.size() ? mActions.get(index) : null;
    }

    public final boolean hasAction(String action) {
        return action != null && mActions.contains(action);
    }

    public final int countCategories() {
        return mCategories.size();
    }

    public final String getCategory(int index) {
        return index >= 0 && index < mCategories.size() ? mCategories.get(index) : null;
    }

    public final boolean hasCategory(String category) {
        return category != null && mCategories.contains(category);
    }

    public final boolean matchAction(String action) {
        return mActions.isEmpty() || hasAction(action);
    }

    public final int match(String action, String type, String scheme, android.net.Uri data,
                           java.util.Set<String> categories, String logTag, int flags) {
        if (!matchAction(action)) return -1;
        if (mDataScheme != null && scheme != null && !mDataScheme.equals(scheme)) return -1;
        return 0;
    }

    @Override
    public String toString() {
        return "IntentFilter{actions=" + mActions + " categories=" + mCategories + "}";
    }
}
