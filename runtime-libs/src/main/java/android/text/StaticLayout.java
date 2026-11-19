package android.text;

/**
 * 静态文本布局（AOSP android.text.StaticLayout 公开面；§31.7 C 档 lib——复杂类
 * 禁 native；行折断 = Layout 贪心网格口径）。
 *
 * <p>兼容两面：经典 &lt;init&gt;（7/8/9 参族）与 Builder API（obtain/setXxx/build）。
 */
public class StaticLayout extends Layout {

    public StaticLayout(CharSequence source, TextPaint paint, int width,
                        Alignment align, float spacingmult, float spacingadd,
                        boolean includepad) {
        this(source, 0, source != null ? source.length() : 0, paint, width, align,
                spacingmult, spacingadd, includepad);
    }

    public StaticLayout(CharSequence source, int bufstart, int bufend, TextPaint paint,
                        int outerwidth, Alignment align, float spacingmult, float spacingadd,
                        boolean includepad) {
        this(source, bufstart, bufend, paint, outerwidth, align, spacingmult, spacingadd,
                includepad, null, outerwidth);
    }

    public StaticLayout(CharSequence source, int bufstart, int bufend, TextPaint paint,
                        int outerwidth, Alignment align, float spacingmult, float spacingadd,
                        boolean includepad, TextUtils$TruncateAt ellipsize, int ellipsizedWidth) {
        super(source, paint, outerwidth, align, spacingmult, spacingadd, includepad,
                ellipsize, ellipsizedWidth);
    }

    /** 构建器（AOSP StaticLayout.Builder 面）。 */
    public static final class Builder {
        private final CharSequence mText;
        private final int mStart;
        private final int mEnd;
        private final TextPaint mPaint;
        private final int mMaxWidth;
        private Alignment mAlignment = Alignment.ALIGN_NORMAL;
        private float mSpacingMult = 1f;
        private float mSpacingAdd = 0f;
        private boolean mIncludePad = true;
        private TextDirectionHeuristic mTextDirection = TextDirectionHeuristics.FIRSTSTRONG_LTR;
        private int mBreakStrategy;
        private int mHyphenationFrequency;
        private int mMaxLines = Integer.MAX_VALUE;
        private TextUtils$TruncateAt mEllipsize;
        private int mEllipsizedWidth;

        private Builder(CharSequence source, int start, int end, TextPaint paint, int maxWidth) {
            this.mText = source != null ? source : "";
            this.mStart = start;
            this.mEnd = end;
            this.mPaint = paint != null ? paint : new TextPaint();
            this.mMaxWidth = maxWidth;
            this.mEllipsizedWidth = maxWidth;
        }

        public static Builder obtain(CharSequence source, int start, int end,
                                     TextPaint paint, int width) {
            return new Builder(source, start, end, paint, width);
        }

        public static Builder obtain(CharSequence source, TextPaint paint, int width) {
            return obtain(source, 0, source != null ? source.length() : 0, paint, width);
        }

        public Builder setText(CharSequence source) {
            return obtain(source, 0, source != null ? source.length() : 0, this.mPaint,
                    this.mMaxWidth);
        }

        public Builder setText(CharSequence source, int start, int end) {
            return obtain(source, start, end, this.mPaint, this.mMaxWidth);
        }

        public Builder setPaint(TextPaint paint) {
            return obtain(this.mText, this.mStart, this.mEnd, paint, this.mMaxWidth);
        }

        public Builder setWidth(int width) {
            return obtain(this.mText, this.mStart, this.mEnd, this.mPaint, width);
        }

        public Builder setAlignment(Alignment alignment) {
            this.mAlignment = alignment != null ? alignment : Alignment.ALIGN_NORMAL;
            return this;
        }

        public Builder setTextDirection(TextDirectionHeuristic textDirection) {
            this.mTextDirection = textDirection != null ? textDirection
                    : TextDirectionHeuristics.FIRSTSTRONG_LTR;
            return this;
        }

        public Builder setLineSpacing(float spacingAdd, float spacingMult) {
            this.mSpacingAdd = spacingAdd;
            this.mSpacingMult = spacingMult;
            return this;
        }

        public Builder setIncludePad(boolean includePad) {
            this.mIncludePad = includePad;
            return this;
        }

        public Builder setEllipsize(TextUtils$TruncateAt ellipsize) {
            this.mEllipsize = ellipsize;
            return this;
        }

        public Builder setEllipsizedWidth(int ellipsizedWidth) {
            this.mEllipsizedWidth = ellipsizedWidth;
            return this;
        }

        public Builder setMaxLines(int maxLines) {
            this.mMaxLines = maxLines;
            return this;
        }

        public Builder setBreakStrategy(int breakStrategy) {
            this.mBreakStrategy = breakStrategy;
            return this;
        }

        public Builder setHyphenationFrequency(int hyphenationFrequency) {
            this.mHyphenationFrequency = hyphenationFrequency;
            return this;
        }

        public int getMaxLines() {
            return this.mMaxLines;
        }

        public int getEllipsizedWidth() {
            return this.mEllipsizedWidth;
        }

        public StaticLayout build() {
            CharSequence text = this.mText.subSequence(this.mStart, this.mEnd);
            return new StaticLayout(text, 0, text.length(), this.mPaint, this.mMaxWidth,
                    this.mAlignment, this.mSpacingMult, this.mSpacingAdd, this.mIncludePad,
                    this.mEllipsize, this.mEllipsizedWidth);
        }
    }
}
