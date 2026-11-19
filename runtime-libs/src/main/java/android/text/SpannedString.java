package android.text;

/**
 * 不可变带标记字符串（AOSP android.text.SpannedString；§31.7 C 档 lib）。
 */
public class SpannedString extends SpannableStringBuilder {

    public SpannedString(CharSequence source) {
        super(source);
    }

    public SpannedString(CharSequence source, int start, int end) {
        super(source, start, end);
    }

    public static SpannedString valueOf(CharSequence source) {
        return source instanceof SpannedString
                ? (SpannedString) source : new SpannedString(source);
    }
}
