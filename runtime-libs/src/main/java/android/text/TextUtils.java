package android.text;

import android.os.Parcel;
import android.os.Parcelable;

import java.util.Locale;

public class TextUtils {

    /** CharSequence 打包 creator（AOSP TextUtils.CHAR_SEQUENCE_CREATOR 同形）。 */
    public static final Parcelable.Creator<CharSequence> CHAR_SEQUENCE_CREATOR =
            new Parcelable.Creator<CharSequence>() {
                @Override
                public CharSequence createFromParcel(Parcel source) {
                    String s = source.readString();
                    return s != null ? s : "";
                }

                @Override
                public CharSequence[] newArray(int size) {
                    return new CharSequence[size];
                }
            };

    public static void writeToParcel(CharSequence cs, Parcel p, int parcelableFlags) {
        p.writeString(cs != null ? cs.toString() : null);
    }

    public static String substring(CharSequence source, int start, int end) {
        if (source == null) return null;
        if (start < 0) start = 0;
        if (end > source.length()) end = source.length();
        return source.subSequence(start, end).toString();
    }

    public static CharSequence concat(CharSequence... text) {
        StringBuilder sb = new StringBuilder();
        if (text != null) {
            for (CharSequence cs : text) {
                if (cs != null) sb.append(cs);
            }
        }
        return sb.toString();
    }

    public static String htmlEncode(String s) {
        if (s == null) return null;
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '<':
                    sb.append("&lt;");
                    break;
                case '>':
                    sb.append("&gt;");
                    break;
                case '&':
                    sb.append("&amp;");
                    break;
                case '\'':
                    sb.append("&#39;");
                    break;
                case '"':
                    sb.append("&quot;");
                    break;
                default:
                    sb.append(c);
            }
        }
        return sb.toString();
    }

    public static int indexOf(CharSequence s, char ch, int start, int end) {
        if (s == null) return -1;
        if (start < 0) start = 0;
        if (end > s.length()) end = s.length();
        for (int i = start; i < end; i++) {
            if (s.charAt(i) == ch) return i;
        }
        return -1;
    }

    public static int getLayoutDirectionFromLocale(Locale locale) {
        if (locale == null) return 0;
        String lang = locale.getLanguage();
        if ("ar".equals(lang) || "fa".equals(lang) || "he".equals(lang)
                || "iw".equals(lang) || "ur".equals(lang) || "yi".equals(lang)) {
            return 1;  // View.LAYOUT_DIRECTION_RTL
        }
        return 0;  // View.LAYOUT_DIRECTION_LTR
    }

    public static int getTrimmedLength(CharSequence s) {
        if (s == null) return 0;
        int start = 0;
        int end = s.length();
        while (start < end && s.charAt(start) <= ' ') start++;
        while (end > start && s.charAt(end - 1) <= ' ') end--;
        return end - start;
    }

    public static boolean isGraphic(CharSequence str) {
        if (str == null) return false;
        for (int i = 0, n = str.length(); i < n; i++) {
            int c = str.charAt(i);
            if (!Character.isWhitespace(c) && !Character.isISOControl(c)) return true;
        }
        return false;
    }

    public static boolean isBlank(CharSequence str) {
        return !isGraphic(str);
    }

    public static boolean isDigitsOnly(CharSequence value) {
        if (value == null || value.length() == 0) return false;
        for (int index = 0; index < value.length(); index++) {
            if (!Character.isDigit(value.charAt(index))) return false;
        }
        return true;
    }

    public static boolean isEmpty(CharSequence value) {
        return value == null || value.length() == 0;
    }

    public static boolean equals(CharSequence left, CharSequence right) {
        if (left == right) return true;
        if (left == null || right == null || left.length() != right.length())
            return false;
        for (int index = 0; index < left.length(); index++) {
            if (left.charAt(index) != right.charAt(index)) return false;
        }
        return true;
    }

    public static String join(CharSequence delimiter, Object[] tokens) {
        if (tokens == null || tokens.length == 0) return "";
        StringBuilder result = new StringBuilder();
        for (int index = 0; index < tokens.length; index++) {
            if (index != 0) result.append(delimiter);
            result.append(tokens[index]);
        }
        return result.toString();
    }

    public static String join(
            CharSequence delimiter, Iterable<?> tokens) {
        StringBuilder result = new StringBuilder();
        boolean first = true;
        for (Object token : tokens) {
            if (!first) result.append(delimiter);
            result.append(token);
            first = false;
        }
        return result.toString();
    }

    public static String[] split(String text, String expression) {
        if (isEmpty(text)) return new String[0];
        return text.split(expression);
    }
}
