package java.lang;

/*
 * （生成文件，勿手改；GenCharData 产出）java.lang 字符数据面（§31.7 C 档 java.*
 * → lib）。语义源：JDK Character 公开面全量导出（通用类别/大小写/数值/方向性）。
 * 表编码：区间表 hex 串 3 元组 (start,end,value) 升序不相交；映射表 2 元组
 * (src,dst) 升序（dst==src 省略）；bool 区间表 2 元组 (start,end)。
 */
final class CharacterData02 extends CharacterData {
    static final CharacterData02 instance = new CharacterData02();

    private static final int[] CAT = parse(
        "02000002a6df00000502a6e002a6ff00000002a70002b73900000502b73a02b73f00000002b74002b81d00000502b81e02b81f00000002b82002cea100000502cea202ceaf00000002ceb002ebe000000502ebe102ebef00000002ebf002ee5d00000502ee5e02f7ff00000002f80002fa1d00000502fa1e02ffff000000");

    private static final int[] DIGITB = parse();

    private static final int[] LOWERB = parse();

    private static final int[] UPPERB = parse();

    private static final int[] WHITEB = parse();

    private static final int[] MIRRORB = parse();

    private static final int[] JSTARTB = parse(
        "02000002a6df02a70002b73902b74002b81d02b82002cea102ceb002ebe002ebf002ee5d02f80002fa1d");

    private static final int[] JPARTB = parse(
        "02000002a6df02a70002b73902b74002b81d02b82002cea102ceb002ebe002ebf002ee5d02f80002fa1d");

    private static final int[] USTARTB = parse(
        "02000002a6df02a70002b73902b74002b81d02b82002cea102ceb002ebe002ebf002ee5d02f80002fa1d");

    private static final int[] UPARTB = parse(
        "02000002a6df02a70002b73902b74002b81d02b82002cea102ceb002ebe002ebf002ee5d02f80002fa1d");

    private static final int[] IGNB = parse();

    private static final int[] LOWERM = parse();

    private static final int[] UPPERM = parse();

    private static final int[] TITLEM = parse();

    private static final int[] DIGITM = parse();

    private static final int[] NUMM = parse(
        "02f890000009");

    private static final int[] DIR = parse(
        "02000002a6df00000002a6e002a6ffffffffff02a70002b73900000002b73a02b73fffffffff02b74002b81d00000002b81e02b81fffffffff02b82002cea100000002cea202ceafffffffff02ceb002ebe000000002ebe102ebefffffffff02ebf002ee5d00000002ee5e02f7ffffffffff02f80002fa1d00000002fa1e02ffffffffffff");

    private static final int[] MULTIU = parse();

    private static int rangeLookup(int[] t, int ch, int dflt) {
        int lo = 0, hi = t.length / 3 - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int s = t[mid * 3], e = t[mid * 3 + 1];
            if (ch < s) hi = mid - 1;
            else if (ch > e) lo = mid + 1;
            else return t[mid * 3 + 2];
        }
        return dflt;
    }

    private static int mapLookup(int[] t, int ch) {
        int lo = 0, hi = t.length / 2 - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int s = t[mid * 2];
            if (ch < s) hi = mid - 1;
            else if (ch > s) lo = mid + 1;
            else return t[mid * 2 + 1];
        }
        return -1;
    }

    private static boolean boolLookup(int[] t, int ch) {
        int lo = 0, hi = t.length / 2 - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int s = t[mid * 2], e = t[mid * 2 + 1];
            if (ch < s) hi = mid - 1;
            else if (ch > e) lo = mid + 1;
            else return true;
        }
        return false;
    }

    private static int[] parse(String... chunks) {
        int n = 0;
        for (int i = 0; i < chunks.length; i++) n += chunks[i].length() / 6;
        int[] t = new int[n];
        int k = 0;
        for (int ci = 0; ci < chunks.length; ci++) {
            String c = chunks[ci];
            for (int i = 0; i + 6 <= c.length(); i += 6) {
                int v = 0;
                for (int j = i; j < i + 6; j++) {
                    char ch = c.charAt(j);
                    v = (v << 4) | (ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
                }
                t[k++] = v;
            }
        }
        return t;
    }


    @Override
    int getProperties(int ch) {
        // 零消费方（CharacterData 内部面；VM 内无 getProperties 调用点）——占位 0。
        return 0;
    }

    @Override
    int getType(int ch) {
        return rangeLookup(CAT, ch, Character.UNASSIGNED);
    }

    @Override
    boolean isDigit(int ch) {
        return boolLookup(DIGITB, ch);
    }

    @Override
    boolean isLowerCase(int ch) {
        return boolLookup(LOWERB, ch);
    }

    @Override
    boolean isUpperCase(int ch) {
        return boolLookup(UPPERB, ch);
    }

    @Override
    boolean isWhitespace(int ch) {
        return boolLookup(WHITEB, ch);
    }

    @Override
    boolean isMirrored(int ch) {
        return boolLookup(MIRRORB, ch);
    }

    @Override
    boolean isJavaIdentifierStart(int ch) {
        return boolLookup(JSTARTB, ch);
    }

    @Override
    boolean isJavaIdentifierPart(int ch) {
        return boolLookup(JPARTB, ch);
    }

    @Override
    boolean isUnicodeIdentifierStart(int ch) {
        return boolLookup(USTARTB, ch);
    }

    @Override
    boolean isUnicodeIdentifierPart(int ch) {
        return boolLookup(UPARTB, ch);
    }

    @Override
    boolean isIdentifierIgnorable(int ch) {
        return boolLookup(IGNB, ch);
    }

    @Override
    int toLowerCase(int ch) {
        int v = mapLookup(LOWERM, ch);
        return v == -1 ? ch : v;
    }

    @Override
    int toUpperCase(int ch) {
        int v = mapLookup(UPPERM, ch);
        return v == -1 ? ch : v;
    }

    @Override
    int toTitleCase(int ch) {
        int v = mapLookup(TITLEM, ch);
        return v == -1 ? ch : v;
    }

    @Override
    int toUpperCaseEx(int ch) {
        int i = multiIndex(ch);
        return i < 0 ? toUpperCase(ch) : -1;
    }

    @Override
    char[] toUpperCaseCharArray(int ch) {
        int i = multiIndex(ch);
        if (i < 0) return null;
        int n = MULTIU[i + 1];
        char[] out = new char[n];
        for (int j = 0; j < n; j++) out[j] = (char) MULTIU[i + 2 + j];
        return out;
    }

    private static int multiIndex(int ch) {
        int lo = 0, hi = MULTIU.length / 5 - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int s = MULTIU[mid * 5];
            if (ch < s) hi = mid - 1;
            else if (ch > s) lo = mid + 1;
            else return mid * 5;
        }
        return -1;
    }

    @Override
    int digit(int ch, int radix) {
        int v = mapLookup(DIGITM, ch);
        return (v >= 0 && v < radix) ? v : -1;
    }

    @Override
    int getNumericValue(int ch) {
        return mapLookup(NUMM, ch);
    }

    @Override
    byte getDirectionality(int ch) {
        return (byte) rangeLookup(DIR, ch, -1);
    }
}
