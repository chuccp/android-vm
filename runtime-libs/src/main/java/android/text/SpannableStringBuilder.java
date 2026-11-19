package android.text;

import java.util.ArrayList;

/**
 * 可标记字符串构建器（AOSP android.text.SpannableStringBuilder 公开面；
 * §31.7 C 档 lib——复杂类禁 native：StringBuilder 文本态 + span 表纯逻辑）。
 */
public class SpannableStringBuilder implements Spannable, Appendable, CharSequence {

    private final StringBuilder mText = new StringBuilder();
    private final ArrayList<Object> mSpans = new ArrayList<Object>();
    private final ArrayList<int[]> mSpanRange = new ArrayList<int[]>();  // {start,end,flags}

    public SpannableStringBuilder() {
    }

    public SpannableStringBuilder(CharSequence text) {
        if (text != null) this.mText.append(text);
    }

    public SpannableStringBuilder(CharSequence text, int start, int end) {
        if (text != null) this.mText.append(text, start, end);
    }

    public static SpannableStringBuilder valueOf(CharSequence source) {
        return source instanceof SpannableStringBuilder
                ? (SpannableStringBuilder) source : new SpannableStringBuilder(source);
    }

    @Override
    public int length() {
        return this.mText.length();
    }

    @Override
    public char charAt(int index) {
        return this.mText.charAt(index);
    }

    @Override
    public CharSequence subSequence(int start, int end) {
        return this.mText.substring(start, end);
    }

    @Override
    public String toString() {
        return this.mText.toString();
    }

    @Override
    public SpannableStringBuilder append(CharSequence text) {
        this.mText.append(text);
        return this;
    }

    @Override
    public SpannableStringBuilder append(CharSequence text, int start, int end) {
        this.mText.append(text, start, end);
        return this;
    }

    public SpannableStringBuilder append(CharSequence text, Object what, int flags) {
        int start = this.mText.length();
        this.mText.append(text);
        setSpan(what, start, this.mText.length(), flags);
        return this;
    }

    @Override
    public SpannableStringBuilder append(char text) {
        this.mText.append(text);
        return this;
    }

    public SpannableStringBuilder insert(int where, CharSequence text) {
        this.mText.insert(where, text);
        shiftSpans(where, text != null ? text.length() : 0);
        return this;
    }

    public SpannableStringBuilder insert(int where, CharSequence text, int start, int end) {
        int len = end - start;
        this.mText.insert(where, text, start, end);
        shiftSpans(where, len);
        return this;
    }

    public SpannableStringBuilder delete(int start, int end) {
        this.mText.delete(start, end);
        return this;
    }

    public SpannableStringBuilder replace(int start, int end, CharSequence text) {
        this.mText.replace(start, end, text != null ? text.toString() : "");
        return this;
    }

    public SpannableStringBuilder replace(int start, int end, CharSequence text, int tstart,
                                          int tend) {
        this.mText.replace(start, end, text != null ? text.subSequence(tstart, tend).toString() : "");
        return this;
    }

    public SpannableStringBuilder clear() {
        this.mText.setLength(0);
        this.mSpans.clear();
        this.mSpanRange.clear();
        return this;
    }

    public SpannableStringBuilder clearSpans() {
        this.mSpans.clear();
        this.mSpanRange.clear();
        return this;
    }

    @Override
    public void setSpan(Object what, int start, int end, int flags) {
        if (what == null) return;
        this.mSpans.add(what);
        this.mSpanRange.add(new int[] {start, end, flags});
    }

    @Override
    public void removeSpan(Object what) {
        for (int i = this.mSpans.size() - 1; i >= 0; i--) {
            if (this.mSpans.get(i) == what) {
                this.mSpans.remove(i);
                this.mSpanRange.remove(i);
            }
        }
    }

    @Override
    public int getSpanStart(Object tag) {
        int i = this.mSpans.indexOf(tag);
        return i >= 0 ? this.mSpanRange.get(i)[0] : -1;
    }

    @Override
    public int getSpanEnd(Object tag) {
        int i = this.mSpans.indexOf(tag);
        return i >= 0 ? this.mSpanRange.get(i)[1] : -1;
    }

    @Override
    public int getSpanFlags(Object tag) {
        int i = this.mSpans.indexOf(tag);
        return i >= 0 ? this.mSpanRange.get(i)[2] : 0;
    }

    @Override
    @SuppressWarnings("unchecked")
    public <T> T[] getSpans(int start, int end, Class<T> type) {
        ArrayList<T> out = new ArrayList<T>();
        for (int i = 0; i < this.mSpans.size(); i++) {
            int[] r = this.mSpanRange.get(i);
            if (r[0] >= start && r[1] <= end) {
                Object s = this.mSpans.get(i);
                if (type == null || type.isInstance(s)) {
                    out.add((T) s);
                }
            }
        }
        T[] arr = (T[]) java.lang.reflect.Array.newInstance(
                type != null ? type : Object.class, out.size());
        return out.toArray(arr);
    }

    @Override
    public int nextSpanTransition(int start, int limit, Class type) {
        int next = limit;
        for (int i = 0; i < this.mSpans.size(); i++) {
            int[] r = this.mSpanRange.get(i);
            if (r[0] > start && r[0] < next) next = r[0];
            if (r[1] > start && r[1] < next) next = r[1];
        }
        return next;
    }

    public int getSpanEnd(Object what, int defValue) {
        int v = getSpanEnd(what);
        return v >= 0 ? v : defValue;
    }

    private void shiftSpans(int from, int delta) {
        for (int i = 0; i < this.mSpanRange.size(); i++) {
            int[] r = this.mSpanRange.get(i);
            if (r[0] >= from) r[0] += delta;
            if (r[1] >= from) r[1] += delta;
        }
    }
}
