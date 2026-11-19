package android.text;

/**
 * 带标记文本接口（AOSP android.text.Spanned；§31.7 C 档 lib 接口面）。
 */
public interface Spanned extends CharSequence {
    int SPAN_INCLUSIVE_EXCLUSIVE = 0x0011;
    int SPAN_INCLUSIVE_INCLUSIVE = 0x0012;
    int SPAN_EXCLUSIVE_EXCLUSIVE = 0x0021;
    int SPAN_EXCLUSIVE_INCLUSIVE = 0x0022;
    int SPAN_COMPOSING = 0x100;
    int SPAN_INTERMEDIATE = 0x200;
    int SPAN_MARK_MARK = 0x11;
    int SPAN_MARK_POINT = 0x12;
    int SPAN_POINT_MARK = 0x21;
    int SPAN_POINT_POINT = 0x22;
    int SPAN_PARAGRAPH = 0x33;

    int getSpanStart(Object tag);

    int getSpanEnd(Object tag);

    int getSpanFlags(Object tag);

    <T> T[] getSpans(int start, int end, Class<T> type);

    int nextSpanTransition(int start, int limit, Class type);
}
