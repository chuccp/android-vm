package android.net;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Set;

/**
 * UrlQuerySanitizer（AOSP android.net.UrlQuerySanitizer 公开面；§31.7 C 档 lib）。
 *
 * 纯字符串解析/清洗逻辑，无 VM 布局/native 需求：解析 URL 的 query 段，
 * 按注册的 ValueSanitizer（默认 AllIllegal）解码并清洗参数值，保留参数
 * 出现顺序（getParameterList 可含重复参数）。
 *
 * 语义与 AOSP 保持一致：
 *   - parseUrl/parseQuery 先 clear()，query 以 '&' 分隔、空 token 跳过；
 *   - '+' 解码为空格，合法 "%xx" 解码，非法转义原样保留；
 *   - 未注册参数默认丢弃，setAllowUnregisteredParamaters(true) 后走默认
 *     sanitizer（AllIllegal）；
 *   - 重复参数默认取最后一个（setPreferFirstRepeatedParameter 可改为第一个）。
 */
public class UrlQuerySanitizer {

    /** 参数-值元组（未编码）。 */
    public class ParameterValuePair {
        public ParameterValuePair(String parameter, String value) {
            mParameter = parameter;
            mValue = value;
        }

        /** 未编码的参数名。 */
        public String mParameter;
        /** 未编码的参数值。 */
        public String mValue;
    }

    /** 单个 query 值的清洗函数。 */
    public static interface ValueSanitizer {
        String sanitize(String value);
    }

    /** 按字符类别做清洗：非法字符替换为 ' ' 或 '_'（取决于空格是否合法）。 */
    public static class IllegalCharacterValueSanitizer implements ValueSanitizer {
        public static final int SPACE_OK = 1 << 0;
        public static final int OTHER_WHITESPACE_OK = 1 << 1;
        public static final int NON_7_BIT_ASCII_OK = 1 << 2;
        public static final int DQUOTE_OK = 1 << 3;
        public static final int SQUOTE_OK = 1 << 4;
        public static final int LT_OK = 1 << 5;
        public static final int GT_OK = 1 << 6;
        public static final int AMP_OK = 1 << 7;
        public static final int PCT_OK = 1 << 8;
        public static final int NUL_OK = 1 << 9;
        public static final int SCRIPT_URL_OK = 1 << 10;

        public static final int ALL_OK = 0x7ff;
        public static final int ALL_WHITESPACE_OK = SPACE_OK | OTHER_WHITESPACE_OK;

        public static final int ALL_ILLEGAL = 0;
        public static final int ALL_BUT_NUL_LEGAL = ALL_OK & ~NUL_OK;
        public static final int ALL_BUT_WHITESPACE_LEGAL =
                ALL_OK & ~(ALL_WHITESPACE_OK | NUL_OK);
        public static final int URL_LEGAL =
                NON_7_BIT_ASCII_OK | SQUOTE_OK | AMP_OK | PCT_OK;
        public static final int URL_AND_SPACE_LEGAL = URL_LEGAL | SPACE_OK;
        public static final int AMP_LEGAL = AMP_OK;
        public static final int AMP_AND_SPACE_LEGAL = AMP_OK | SPACE_OK;
        public static final int SPACE_LEGAL = SPACE_OK;
        public static final int ALL_BUT_NUL_AND_ANGLE_BRACKETS_LEGAL =
                ALL_OK & ~(NUL_OK | LT_OK | GT_OK);

        private static final String JAVASCRIPT_PREFIX = "javascript:";
        private static final String VBSCRIPT_PREFIX = "vbscript:";
        private static final int MIN_SCRIPT_PREFIX_LENGTH =
                Math.min(JAVASCRIPT_PREFIX.length(), VBSCRIPT_PREFIX.length());

        private final int mFlags;

        public IllegalCharacterValueSanitizer(int flags) {
            mFlags = flags;
        }

        public String sanitize(String value) {
            if (value == null) {
                return null;
            }
            int length = value.length();
            if ((mFlags & SCRIPT_URL_OK) == 0 && length >= MIN_SCRIPT_PREFIX_LENGTH) {
                // AOSP 先 toLowerCase(Locale.ROOT) 再 startsWith；对纯 ASCII
                // 前缀而言与 ignoreCase 的区域比较等价，且不产生临时串。
                if (value.regionMatches(true, 0, JAVASCRIPT_PREFIX, 0,
                        JAVASCRIPT_PREFIX.length())
                        || value.regionMatches(true, 0, VBSCRIPT_PREFIX, 0,
                        VBSCRIPT_PREFIX.length())) {
                    return "";
                }
            }
            if ((mFlags & ALL_WHITESPACE_OK) == 0) {
                value = trimWhitespace(value);
                length = value.length();
            }
            StringBuilder builder = new StringBuilder(length);
            for (int i = 0; i < length; i++) {
                char c = value.charAt(i);
                if (!characterIsLegal(c)) {
                    c = (mFlags & SPACE_OK) != 0 ? ' ' : '_';
                }
                builder.append(c);
            }
            return builder.toString();
        }

        private String trimWhitespace(String value) {
            int start = 0;
            int last = value.length() - 1;
            int end = last;
            while (start <= end && isWhitespace(value.charAt(start))) {
                start++;
            }
            while (end >= start && isWhitespace(value.charAt(end))) {
                end--;
            }
            if (start == 0 && end == last) {
                return value;
            }
            return value.substring(start, end + 1);
        }

        private boolean isWhitespace(char c) {
            switch (c) {
                case ' ':
                case '\t':
                case '\f':
                case '\n':
                case '\r':
                case 11: /* VT */
                    return true;
                default:
                    return false;
            }
        }

        private boolean characterIsLegal(char c) {
            switch (c) {
                case ' ':
                    return (mFlags & SPACE_OK) != 0;
                case '\t':
                case '\f':
                case '\n':
                case '\r':
                case 11: /* VT */
                    return (mFlags & OTHER_WHITESPACE_OK) != 0;
                case '\"':
                    return (mFlags & DQUOTE_OK) != 0;
                case '\'':
                    return (mFlags & SQUOTE_OK) != 0;
                case '<':
                    return (mFlags & LT_OK) != 0;
                case '>':
                    return (mFlags & GT_OK) != 0;
                case '&':
                    return (mFlags & AMP_OK) != 0;
                case '%':
                    return (mFlags & PCT_OK) != 0;
                case '\0':
                    return (mFlags & NUL_OK) != 0;
                default:
                    return (c >= 32 && c < 127)
                            || ((c >= 128) && (mFlags & NON_7_BIT_ASCII_OK) != 0);
            }
        }
    }

    private static final ValueSanitizer sAllIllegal =
            new IllegalCharacterValueSanitizer(IllegalCharacterValueSanitizer.ALL_ILLEGAL);
    private static final ValueSanitizer sAllButNulLegal =
            new IllegalCharacterValueSanitizer(
                    IllegalCharacterValueSanitizer.ALL_BUT_NUL_LEGAL);
    private static final ValueSanitizer sAllButWhitespaceLegal =
            new IllegalCharacterValueSanitizer(
                    IllegalCharacterValueSanitizer.ALL_BUT_WHITESPACE_LEGAL);
    private static final ValueSanitizer sURLLegal =
            new IllegalCharacterValueSanitizer(IllegalCharacterValueSanitizer.URL_LEGAL);
    private static final ValueSanitizer sUrlAndSpaceLegal =
            new IllegalCharacterValueSanitizer(
                    IllegalCharacterValueSanitizer.URL_AND_SPACE_LEGAL);
    private static final ValueSanitizer sAmpLegal =
            new IllegalCharacterValueSanitizer(IllegalCharacterValueSanitizer.AMP_LEGAL);
    private static final ValueSanitizer sAmpAndSpaceLegal =
            new IllegalCharacterValueSanitizer(
                    IllegalCharacterValueSanitizer.AMP_AND_SPACE_LEGAL);
    private static final ValueSanitizer sSpaceLegal =
            new IllegalCharacterValueSanitizer(IllegalCharacterValueSanitizer.SPACE_LEGAL);
    private static final ValueSanitizer sAllButNulAndAngleBracketsLegal =
            new IllegalCharacterValueSanitizer(
                    IllegalCharacterValueSanitizer.ALL_BUT_NUL_AND_ANGLE_BRACKETS_LEGAL);

    public static final ValueSanitizer getAllIllegal() {
        return sAllIllegal;
    }

    public static final ValueSanitizer getAllButNulLegal() {
        return sAllButNulLegal;
    }

    public static final ValueSanitizer getAllButWhitespaceLegal() {
        return sAllButWhitespaceLegal;
    }

    public static final ValueSanitizer getUrlLegal() {
        return sURLLegal;
    }

    public static final ValueSanitizer getUrlAndSpaceLegal() {
        return sUrlAndSpaceLegal;
    }

    public static final ValueSanitizer getAmpLegal() {
        return sAmpLegal;
    }

    public static final ValueSanitizer getAmpAndSpaceLegal() {
        return sAmpAndSpaceLegal;
    }

    public static final ValueSanitizer getSpaceLegal() {
        return sSpaceLegal;
    }

    public static final ValueSanitizer getAllButNulAndAngleBracketsLegal() {
        return sAllButNulAndAngleBracketsLegal;
    }

    private final HashMap<String, ValueSanitizer> mSanitizers =
            new HashMap<String, ValueSanitizer>();
    private final HashMap<String, String> mEntries = new HashMap<String, String>();
    private final ArrayList<ParameterValuePair> mEntriesList =
            new ArrayList<ParameterValuePair>();
    private boolean mAllowUnregisteredParamaters;
    private boolean mPreferFirstRepeatedParameter;
    private ValueSanitizer mUnregisteredParameterValueSanitizer = getAllIllegal();

    /** 默认：不允许未注册参数；重复参数取最后一个；默认 sanitizer 为 AllIllegal。 */
    public UrlQuerySanitizer() {
    }

    /** 便捷构造：允许未注册参数，并立即解析 url。 */
    public UrlQuerySanitizer(String url) {
        setAllowUnregisteredParamaters(true);
        parseUrl(url);
    }

    public ValueSanitizer getUnregisteredParameterValueSanitizer() {
        return mUnregisteredParameterValueSanitizer;
    }

    public void setUnregisteredParameterValueSanitizer(ValueSanitizer sanitizer) {
        mUnregisteredParameterValueSanitizer = sanitizer;
    }

    /** 取 URL 的 query 段（无 '?' 视为空串）后交给 parseQuery。 */
    public void parseUrl(String url) {
        int queryIndex = url.indexOf('?');
        String query = queryIndex >= 0 ? url.substring(queryIndex + 1) : "";
        parseQuery(query);
    }

    /** 以 '&' 分隔解析 query；空 token 跳过；无 '=' 的值视为空串。 */
    public void parseQuery(String query) {
        clear();
        int length = query.length();
        int start = 0;
        while (start < length) {
            int amp = query.indexOf('&', start);
            int end = amp < 0 ? length : amp;
            if (end > start) {
                String attributeValuePair = query.substring(start, end);
                int assignmentIndex = attributeValuePair.indexOf('=');
                if (assignmentIndex < 0) {
                    parseEntry(attributeValuePair, "");
                } else {
                    parseEntry(attributeValuePair.substring(0, assignmentIndex),
                            attributeValuePair.substring(assignmentIndex + 1));
                }
            }
            if (amp < 0) {
                break;
            }
            start = amp + 1;
        }
    }

    /** 当前 query 中所有参数名（只读）。 */
    public Set<String> getParameterSet() {
        return mEntries.keySet();
    }

    /** 按出现顺序排列的参数-值对（可重复；只读）。 */
    public List<ParameterValuePair> getParameterList() {
        return mEntriesList;
    }

    public boolean hasParameter(String parameter) {
        return mEntries.containsKey(parameter);
    }

    public String getValue(String parameter) {
        return mEntries.get(parameter);
    }

    /** 注册某参数的 sanitizer；传 null 取消注册。 */
    public void registerParameter(String parameter, ValueSanitizer valueSanitizer) {
        if (valueSanitizer == null) {
            mSanitizers.remove(parameter);
        }
        mSanitizers.put(parameter, valueSanitizer);
    }

    public void registerParameters(String[] parameters, ValueSanitizer valueSanitizer) {
        for (int i = 0; i < parameters.length; i++) {
            mSanitizers.put(parameters[i], valueSanitizer);
        }
    }

    public void setAllowUnregisteredParamaters(boolean allowUnregisteredParamaters) {
        mAllowUnregisteredParamaters = allowUnregisteredParamaters;
    }

    public boolean getAllowUnregisteredParamaters() {
        return mAllowUnregisteredParamaters;
    }

    public void setPreferFirstRepeatedParameter(boolean preferFirstRepeatedParameter) {
        mPreferFirstRepeatedParameter = preferFirstRepeatedParameter;
    }

    public boolean getPreferFirstRepeatedParameter() {
        return mPreferFirstRepeatedParameter;
    }

    /** 解码参数与值，按有效 sanitizer 清洗后登记。 */
    protected void parseEntry(String parameter, String value) {
        String unescapedParameter = unescape(parameter);
        ValueSanitizer valueSanitizer = getEffectiveValueSanitizer(unescapedParameter);
        if (valueSanitizer == null) {
            return;
        }
        String unescapedValue = unescape(value);
        String sanitizedValue = valueSanitizer.sanitize(unescapedValue);
        addSanitizedEntry(unescapedParameter, sanitizedValue);
    }

    protected void addSanitizedEntry(String parameter, String value) {
        mEntriesList.add(new ParameterValuePair(parameter, value));
        if (mPreferFirstRepeatedParameter) {
            if (mEntries.containsKey(parameter)) {
                return;
            }
        }
        mEntries.put(parameter, value);
    }

    public ValueSanitizer getValueSanitizer(String parameter) {
        return mSanitizers.get(parameter);
    }

    /** 注册值优先；未注册时仅在允许未注册参数时回落到默认 sanitizer。 */
    public ValueSanitizer getEffectiveValueSanitizer(String parameter) {
        ValueSanitizer sanitizer = getValueSanitizer(parameter);
        if (sanitizer == null && mAllowUnregisteredParamaters) {
            sanitizer = getUnregisteredParameterValueSanitizer();
        }
        return sanitizer;
    }

    /** '+' -> ' '，合法 "%xx" 解码，非法转义原样保留（AOSP 语义）。 */
    public String unescape(String string) {
        int plus = string.indexOf('+');
        int percent = string.indexOf('%');
        int firstEscape = plus < 0 ? percent : (percent < 0 ? plus : Math.min(plus, percent));
        if (firstEscape < 0) {
            return string;
        }
        int length = string.length();
        StringBuilder builder = new StringBuilder(length);
        builder.append(string.substring(0, firstEscape));
        for (int i = firstEscape; i < length; i++) {
            char c = string.charAt(i);
            if (c == '+') {
                c = ' ';
            } else if (c == '%' && i + 2 < length) {
                char c1 = string.charAt(i + 1);
                char c2 = string.charAt(i + 2);
                if (isHexDigit(c1) && isHexDigit(c2)) {
                    c = (char) (decodeHexDigit(c1) * 16 + decodeHexDigit(c2));
                    i += 2;
                }
            }
            builder.append(c);
        }
        return builder.toString();
    }

    protected boolean isHexDigit(char c) {
        return decodeHexDigit(c) >= 0;
    }

    protected int decodeHexDigit(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        } else if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        } else {
            return -1;
        }
    }

    protected void clear() {
        mEntries.clear();
        mEntriesList.clear();
    }
}
