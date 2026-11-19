package android.text;

/**
 * 不可变可标记字符串（AOSP android.text.SpannableString；§31.7 C 档 lib——
 * 文本态 + span 表）。
 */
public class SpannableString extends SpannableStringBuilder {

    public SpannableString(CharSequence source) {
        super(source);
    }

    public SpannableString(CharSequence source, int start, int end) {
        super(source, start, end);
    }

    public static SpannableString valueOf(CharSequence source) {
        return source instanceof SpannableString
                ? (SpannableString) source : new SpannableString(source);
    }

    @Override
    public void setSpan(Object what, int start, int end, int flags) {
        super.setSpan(what, start, end, flags);
    }

    @Override
    public void removeSpan(Object what) {
        super.removeSpan(what);
    }
}
