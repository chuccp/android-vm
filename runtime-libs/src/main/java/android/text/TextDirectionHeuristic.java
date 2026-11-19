package android.text;

/**
 * 文本方向启发式接口（AOSP android.text.TextDirectionHeuristic；§31.7 C 档 lib）。
 */
public interface TextDirectionHeuristic {
    boolean isRtl(char[] array, int start, int count);

    boolean isRtl(CharSequence text, int start, int count);
}
