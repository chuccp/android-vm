package android.text;

/**
 * 文本方向启发式集合（AOSP android.text.TextDirectionHeuristics 公开常量；
 * §31.7 C 档 lib）。LTR/RTL 恒定向；FIRSTSTRONG/ANYRTL 按首强字符判定。
 */
public final class TextDirectionHeuristics {

    private TextDirectionHeuristics() {
    }

    static abstract class TextDirectionHeuristicImpl implements TextDirectionHeuristic {
        private final TextDirectionAlgorithm mAlgorithm;

        TextDirectionHeuristicImpl(TextDirectionAlgorithm algorithm) {
            this.mAlgorithm = algorithm;
        }

        @Override
        public boolean isRtl(char[] array, int start, int count) {
            if (array == null || start < 0 || count < 0 || start + count > array.length) {
                throw new IllegalArgumentException("bad args");
            }
            return this.mAlgorithm == null ? defaultIsRtl() : doCheck(array, start, count);
        }

        @Override
        public boolean isRtl(CharSequence text, int start, int count) {
            if (text == null || start < 0 || count < 0 || start + count > text.length()) {
                throw new IllegalArgumentException("bad args");
            }
            return this.mAlgorithm == null ? defaultIsRtl() : doCheck(text, start, count);
        }

        abstract boolean defaultIsRtl();

        private boolean doCheck(char[] array, int start, int count) {
            return this.mAlgorithm.checkRtl(array, start, count);
        }

        private boolean doCheck(CharSequence text, int start, int count) {
            char[] buf = new char[count];
            for (int i = 0; i < count; i++) {
                buf[i] = text.charAt(start + i);
            }
            return this.mAlgorithm.checkRtl(buf, 0, count);
        }
    }

    interface TextDirectionAlgorithm {
        boolean checkRtl(char[] array, int start, int count);
    }

    static final class TextDirectionHeuristicInternal extends TextDirectionHeuristicImpl {
        private final boolean mDefaultIsRtl;

        TextDirectionHeuristicInternal(TextDirectionAlgorithm algorithm, boolean defaultIsRtl) {
            super(algorithm);
            this.mDefaultIsRtl = defaultIsRtl;
        }

        @Override
        boolean defaultIsRtl() {
            return this.mDefaultIsRtl;
        }
    }

    private static final TextDirectionAlgorithm FIRST_STRONG = new FirstStrong();
    private static final TextDirectionAlgorithm ANY_RTL = new AnyStrong();
    private static final TextDirectionAlgorithm LOCALE_ALGO = new TextDirectionAlgorithm() {
        @Override
        public boolean checkRtl(char[] array, int start, int count) {
            String lang = java.util.Locale.getDefault().getLanguage();
            return lang.equals("ar") || lang.equals("he") || lang.equals("fa")
                    || lang.equals("iw") || lang.equals("ur");
        }
    };

    public static final TextDirectionHeuristic LTR =
            new TextDirectionHeuristicInternal(null, false);

    public static final TextDirectionHeuristic RTL =
            new TextDirectionHeuristicInternal(null, true);

    public static final TextDirectionHeuristic FIRSTSTRONG_LTR =
            new TextDirectionHeuristicInternal(FIRST_STRONG, false);

    public static final TextDirectionHeuristic FIRSTSTRONG_RTL =
            new TextDirectionHeuristicInternal(FIRST_STRONG, true);

    public static final TextDirectionHeuristic ANYRTL_LTR =
            new TextDirectionHeuristicInternal(ANY_RTL, false);

    public static final TextDirectionHeuristic LOCALE =
            new TextDirectionHeuristicInternal(LOCALE_ALGO, false);

    private static class FirstStrong implements TextDirectionAlgorithm {
        FirstStrong() {
        }

        @Override
        public boolean checkRtl(char[] array, int start, int count) {
            for (int i = start, e = start + count; i < e; ++i) {
                byte dir = Character.getDirectionality(array[i]);
                if (dir == Character.DIRECTIONALITY_LEFT_TO_RIGHT) {
                    return false;
                }
                if (dir == Character.DIRECTIONALITY_RIGHT_TO_LEFT
                        || dir == Character.DIRECTIONALITY_RIGHT_TO_LEFT_ARABIC) {
                    return true;
                }
            }
            return false;
        }
    }

    private static class AnyStrong implements TextDirectionAlgorithm {
        AnyStrong() {
        }

        @Override
        public boolean checkRtl(char[] array, int start, int count) {
            boolean haveRtl = false;
            for (int i = start, e = start + count; i < e; ++i) {
                byte dir = Character.getDirectionality(array[i]);
                if (dir == Character.DIRECTIONALITY_LEFT_TO_RIGHT) {
                    return false;
                }
                if (dir == Character.DIRECTIONALITY_RIGHT_TO_LEFT
                        || dir == Character.DIRECTIONALITY_RIGHT_TO_LEFT_ARABIC) {
                    haveRtl = true;
                }
            }
            return haveRtl;
        }
    }
}
