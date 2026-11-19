package android.text;

/**
 * 可标记文本接口（AOSP android.text.Spannable；§31.7 C 档 lib 接口面）。
 */
public interface Spannable extends Spanned {

    void setSpan(Object what, int start, int end, int flags);

    void removeSpan(Object what);
}
