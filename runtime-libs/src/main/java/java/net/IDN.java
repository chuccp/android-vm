package java.net;

/**
 * IDN（RFC 3490）最小自包含实现。
 *
 * 原 OpenJDK 版本依赖 sun.net.idn.StringPrep/Punycode + sun.text.normalizer
 * + uidna.spp 资源，这些面在 runtime-libs 中不存在，整个类被判
 * uncompilable 剔除——OkHttp 的 Util.canonicalizeHost 每次构造 URL 都走
 * IDN.toASCII，类缺失时抛 NoClassDefFoundError，HttpUrl.get 失败，
 * Retrofit 初始化中断（知乎 NetProviderImpl.sRetrofit == null）。
 *
 * 本实现保留 API 形状与 ASCII 语义：ASCII 标签原样返回（STD3 规则按 flag
 * 校验）；非 ASCII 标签内联 Punycode（RFC 3492）编码，前缀 xn--。
 * 差异：不做 nameprep/Unicode 规范化（该面需要 UCharacterIterator 数据表，
 * 属长尾；域名场景的 NFC 输入在绝大多数情况下与编码结果一致）。
 */
public final class IDN {

    public static final int ALLOW_UNASSIGNED = 0x01;

    public static final int USE_STD3_ASCII_RULES = 0x02;

    private static final String ACE_PREFIX = "xn--";
    private static final int ACE_PREFIX_LENGTH = ACE_PREFIX.length();

    private static final int MAX_LABEL_LENGTH = 63;

    // ---- Punycode 参数（RFC 3492 6.1）----
    private static final int BASE = 36;
    private static final int TMIN = 1;
    private static final int TMAX = 26;
    private static final int SKEW = 38;
    private static final int DAMP = 700;
    private static final int INITIAL_BIAS = 72;
    private static final int INITIAL_N = 128;

    private IDN() {}

    public static String toASCII(String input) {
        return toASCII(input, 0);
    }

    public static String toASCII(String input, int flag) {
        if (isRootLabel(input)) {
            return ".";
        }
        StringBuilder out = new StringBuilder();
        int p = 0;
        while (p < input.length()) {
            int q = searchDots(input, p);
            out.append(toASCIIInternal(input.substring(p, q), flag));
            if (q != input.length()) {
                out.append('.');
            }
            p = q + 1;
        }
        return out.toString();
    }

    public static String toUnicode(String input) {
        return toUnicode(input, 0);
    }

    public static String toUnicode(String input, int flag) {
        if (isRootLabel(input)) {
            return ".";
        }
        StringBuilder out = new StringBuilder();
        int p = 0;
        while (p < input.length()) {
            int q = searchDots(input, p);
            out.append(toUnicodeInternal(input.substring(p, q), flag));
            if (q != input.length()) {
                out.append('.');
            }
            p = q + 1;
        }
        return out.toString();
    }

    private static String toASCIIInternal(String label, int flag) {
        if (label.length() == 0) {
            throw new IllegalArgumentException("Empty label is not a legal name");
        }

        boolean isASCII = isAllASCII(label);
        String dest;
        if (isASCII) {
            dest = label;
        } else {
            if (startsWithACEPrefix(label)) {
                throw new IllegalArgumentException("The input starts with the ACE Prefix");
            }
            dest = ACE_PREFIX + punycodeEncode(label);
        }

        boolean useSTD3ASCIIRules = ((flag & USE_STD3_ASCII_RULES) != 0);
        if (useSTD3ASCIIRules) {
            for (int i = 0; i < dest.length(); i++) {
                int c = dest.charAt(i);
                if (isNonLDHAsciiCodePoint(c)) {
                    throw new IllegalArgumentException("Contains non-LDH ASCII characters");
                }
            }
            if (dest.charAt(0) == '-' || dest.charAt(dest.length() - 1) == '-') {
                throw new IllegalArgumentException("Has leading or trailing hyphen");
            }
        }

        if (dest.length() > MAX_LABEL_LENGTH) {
            throw new IllegalArgumentException("The label in the input is too long");
        }
        return dest;
    }

    private static String toUnicodeInternal(String label, int flag) {
        if (!startsWithACEPrefix(label)) {
            // 非 ACE 标签：无 nameprep 数据表，原样返回（对照 OpenJDK 的
            // "非法输入返回 label" 容错面）。
            return label;
        }
        String temp = label.substring(ACE_PREFIX_LENGTH, label.length());
        try {
            String decoded = punycodeDecode(temp);
            // 往返校验（对照 OpenJDK）：解出的串重新编码必须与输入一致，
            // 否则视为伪装 ACE 标签，原样返回。
            String toASCIIOut = toASCII(decoded, flag);
            if (toASCIIOut.equalsIgnoreCase(label)) {
                return decoded;
            }
        } catch (RuntimeException ignored) {
            // 非法 punycode：落回原 label
        }
        return label;
    }

    // ---- Punycode（RFC 3492）----

    private static String punycodeEncode(String input) {
        int n = INITIAL_N;
        int delta = 0;
        int bias = INITIAL_BIAS;
        StringBuilder output = new StringBuilder();

        // 基本码点（< 0x80）按原序输出
        for (int i = 0; i < input.length(); ) {
            int cp = input.codePointAt(i);
            if (cp < 0x80) {
                output.append((char) cp);
            }
            i += Character.charCount(cp);
        }
        int basicCount = output.length();
        int handled = basicCount;
        if (basicCount > 0) {
            output.append('-');
        }

        int totalCodePoints = 0;
        for (int i = 0; i < input.length(); ) {
            int cp = input.codePointAt(i);
            totalCodePoints++;
            i += Character.charCount(cp);
        }

        while (handled < totalCodePoints) {
            int m = Integer.MAX_VALUE;
            for (int i = 0; i < input.length(); ) {
                int cp = input.codePointAt(i);
                if (cp >= n && cp < m) {
                    m = cp;
                }
                i += Character.charCount(cp);
            }
            if (m == Integer.MAX_VALUE) {
                throw new IllegalArgumentException("Illegal input");
            }
            delta += (m - n) * (handled + 1);
            n = m;
            for (int i = 0; i < input.length(); ) {
                int cp = input.codePointAt(i);
                if (cp < n) {
                    delta++;
                } else if (cp == n) {
                    int q = delta;
                    for (int k = BASE; ; k += BASE) {
                        int t = (k <= bias) ? TMIN : ((k >= bias + TMAX) ? TMAX : k - bias);
                        if (q < t) {
                            break;
                        }
                        output.append(encodeDigit(t + (q - t) % (BASE - t)));
                        q = (q - t) / (BASE - t);
                    }
                    output.append(encodeDigit(q));
                    bias = adapt(delta, handled + 1, handled == basicCount);
                    delta = 0;
                    handled++;
                }
                i += Character.charCount(cp);
            }
            delta++;
            n++;
        }
        return output.toString();
    }

    private static String punycodeDecode(String input) {
        int n = INITIAL_N;
        int i = 0;
        int bias = INITIAL_BIAS;
        StringBuilder output = new StringBuilder();

        int delim = input.lastIndexOf('-');
        int pos = 0;
        if (delim > 0) {
            for (pos = 0; pos < delim; pos++) {
                char c = input.charAt(pos);
                if (c >= 0x80) {
                    throw new IllegalArgumentException("Illegal input");
                }
                output.append(c);
            }
            pos = delim + 1;
        }

        while (pos < input.length()) {
            int oldi = i;
            int w = 1;
            for (int k = BASE; ; k += BASE) {
                if (pos >= input.length()) {
                    throw new IllegalArgumentException("Illegal input");
                }
                int digit = decodeDigit(input.charAt(pos++));
                i += digit * w;
                int t = (k <= bias) ? TMIN : ((k >= bias + TMAX) ? TMAX : k - bias);
                if (digit < t) {
                    break;
                }
                w *= (BASE - t);
            }
            int dlen = output.length();
            bias = adapt(i - oldi, dlen + 1, oldi == 0);
            n += i / (dlen + 1);
            i = i % (dlen + 1);
            if (n > 0x10FFFF || (n >= 0xD800 && n <= 0xDFFF)) {
                throw new IllegalArgumentException("Illegal input");
            }
            output.insert(i, new String(Character.toChars(n)));
            i++;
        }
        return output.toString();
    }

    private static int adapt(int delta, int numPoints, boolean firstTime) {
        delta = firstTime ? delta / DAMP : delta / 2;
        delta += delta / numPoints;
        int k = 0;
        while (delta > ((BASE - TMIN) * TMAX) / 2) {
            delta /= (BASE - TMIN);
            k += BASE;
        }
        return k + (BASE - TMIN + 1) * delta / (delta + SKEW);
    }

    private static char encodeDigit(int digit) {
        return (char) (digit < 26 ? ('a' + digit) : ('0' + digit - 26));
    }

    private static int decodeDigit(char c) {
        if (c >= 'a' && c <= 'z') {
            return c - 'a';
        }
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 26;
        }
        throw new IllegalArgumentException("Illegal input");
    }

    // ---- 标签面辅助 ----

    private static boolean isNonLDHAsciiCodePoint(int ch) {
        return (0x0000 <= ch && ch <= 0x002C) ||
               (0x002E <= ch && ch <= 0x002F) ||
               (0x003A <= ch && ch <= 0x0040) ||
               (0x005B <= ch && ch <= 0x0060) ||
               (0x007B <= ch && ch <= 0x007F);
    }

    private static int searchDots(String s, int start) {
        int i;
        for (i = start; i < s.length(); i++) {
            if (isLabelSeparator(s.charAt(i))) {
                break;
            }
        }
        return i;
    }

    private static boolean isRootLabel(String s) {
        return (s.length() == 1 && isLabelSeparator(s.charAt(0)));
    }

    private static boolean isLabelSeparator(char c) {
        return (c == '.' || c == '。' || c == '．' || c == '｡');
    }

    private static boolean isAllASCII(String input) {
        for (int i = 0; i < input.length(); i++) {
            if (input.charAt(i) > 0x7F) {
                return false;
            }
        }
        return true;
    }

    private static boolean startsWithACEPrefix(String input) {
        if (input.length() < ACE_PREFIX_LENGTH) {
            return false;
        }
        for (int i = 0; i < ACE_PREFIX_LENGTH; i++) {
            if (toASCIILower(input.charAt(i)) != ACE_PREFIX.charAt(i)) {
                return false;
            }
        }
        return true;
    }

    private static char toASCIILower(char c) {
        return (c >= 'A' && c <= 'Z') ? (char) (c + ('a' - 'A')) : c;
    }
}
