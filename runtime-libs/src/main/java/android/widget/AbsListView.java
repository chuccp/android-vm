package android.widget;
import android.view.ActionMode;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

/**
 * 抽象列表视图（AOSP android.widget.AbsListView 公开面；§31.7 C 档 lib 骨架——
 * 列表选择/滚动态）。
 */
public abstract class AbsListView extends AdapterView<ListAdapter> {

    public static final int TRANSCRIPT_MODE_DISABLED = 0;
    public static final int TRANSCRIPT_MODE_NORMAL = 1;
    public static final int TRANSCRIPT_MODE_ALWAYS_SCROLL = 2;

    public static final int CHOICE_MODE_NONE = 0;
    public static final int CHOICE_MODE_SINGLE = 1;
    public static final int CHOICE_MODE_MULTIPLE = 2;
    public static final int CHOICE_MODE_MULTIPLE_MODAL = 3;

    int mTranscriptMode = TRANSCRIPT_MODE_DISABLED;
    int mChoiceMode = CHOICE_MODE_NONE;

    public AbsListView(Context context) {
        super(context);
    }

    public AbsListView(Context context, AttributeSet attrs) {
        this(context);
    }

    public void setTranscriptMode(int mode) {
        this.mTranscriptMode = mode;
    }

    public int getTranscriptMode() {
        return this.mTranscriptMode;
    }

    public void setCacheColorHint(int color) {
    }

    public void setChoiceMode(int choiceMode) {
        this.mChoiceMode = choiceMode;
    }

    public int getChoiceMode() {
        return this.mChoiceMode;
    }

    public void setItemChecked(int position, boolean value) {
    }

    public boolean isItemChecked(int position) {
        return false;
    }

    public int getCheckedItemCount() {
        return 0;
    }

    public void setFastScrollEnabled(boolean enabled) {
    }

    public void setFastScrollAlwaysVisible(boolean alwaysShow) {
    }

    public void setSmoothScrollbarEnabled(boolean enabled) {
    }

    public void setStackFromBottom(boolean stackFromBottom) {
    }

    public void setSelectionFromTop(int position, int y) {
    }

    public void setSelectionAfterHeaderView() {
    }

    public void setOnScrollListener(OnScrollListener l) {
    }

    public void setOnItemClickListener(OnItemClickListener listener) {
        super.setOnItemClickListener(listener);
    }

    public void setMultiChoiceModeListener(MultiChoiceModeListener listener) {
    }

    public void deferNotifyDataSetChanged() {
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        setMeasuredDimension(0, 0);
    }

    @Override
    public void setAdapter(ListAdapter adapter) {
        super.setAdapter(adapter);
    }

    @Override
    public int getCount() {
        return super.getCount();
    }

    @Override
    public ListAdapter getAdapter() {
        return super.getAdapter();
    }

    @Override
    public View getSelectedView() {
        return null;
    }

    @Override
    public void setSelection(int position) {
    }

    /** 滚动监听（AOSP AbsListView.OnScrollListener）。 */
    public interface OnScrollListener {
        int SCROLL_STATE_IDLE = 0;
        int SCROLL_STATE_TOUCH_SCROLL = 1;
        int SCROLL_STATE_FLING = 2;

        void onScrollStateChanged(AbsListView view, int scrollState);

        void onScroll(AbsListView view, int firstVisibleItem, int visibleItemCount,
                      int totalItemCount);
    }

    /** 多选模式监听（AOSP AbsListView.MultiChoiceModeListener）。 */
    public interface MultiChoiceModeListener extends ActionMode.Callback {
        void onItemCheckedStateChanged(ActionMode mode, int position, long id, boolean checked);
    }
}
