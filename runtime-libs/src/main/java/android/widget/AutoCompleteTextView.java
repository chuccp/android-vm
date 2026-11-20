package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 自动完成输入框（AOSP android.widget.AutoCompleteTextView 公开面；§31.7 C 档
 * lib——宿主物化层换成宿主真 AutoCompleteTextView 出画）。
 *
 * <p>guest 侧维护适配器与建议项镜像（mSuggestions：宿主据此重建原生下拉项，
 * 键入仍走 EditText 文本回灌通道）。
 */
public class AutoCompleteTextView extends EditText {

    private ListAdapter mAdapter;
    private int mThreshold = 2;
    private boolean mPopupShowing;
    /** 建议项标题镜像（每次 setAdapter 重建；快照侧按字段名读取）。 */
    private String[] mSuggestions = new String[0];
    private AdapterView.OnItemClickListener mOnItemClickListener;

    public AutoCompleteTextView(Context context) {
        super(context);
    }

    public AutoCompleteTextView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public AutoCompleteTextView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void setAdapter(ListAdapter adapter) {
        this.mAdapter = adapter;
        if (adapter != null) {
            int count = adapter.getCount();
            String[] suggestions = new String[count];
            for (int i = 0; i < count; i++) {
                Object item = adapter.getItem(i);
                suggestions[i] = item == null ? "" : item.toString();
            }
            this.mSuggestions = suggestions;
        } else {
            this.mSuggestions = new String[0];
        }
    }

    public ListAdapter getAdapter() {
        return this.mAdapter;
    }

    public void setThreshold(int threshold) {
        this.mThreshold = threshold;
    }

    public int getThreshold() {
        return this.mThreshold;
    }

    public void showDropDown() {
        this.mPopupShowing = true;
    }

    public void dismissDropDown() {
        this.mPopupShowing = false;
    }

    public boolean isPopupShowing() {
        return this.mPopupShowing;
    }

    public void setOnItemClickListener(AdapterView.OnItemClickListener listener) {
        this.mOnItemClickListener = listener;
    }

    public AdapterView.OnItemClickListener getOnItemClickListener() {
        return this.mOnItemClickListener;
    }

    public void setCompletionHint(CharSequence hint) {
    }

    public CharSequence getCompletionHint() {
        return null;
    }
}
