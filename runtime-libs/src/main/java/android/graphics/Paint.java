package android.graphics;

import android.os.LocaleList;

import java.util.Locale;

/**
 * 画笔（AOSP android.graphics.Paint 公开面；§31.7 C 档 lib——「lib Java + native 缝」
 * 目标态的 lib 侧字段态/测量面）。
 *
 * <p>测量缝（与 render 管线对齐，docs 10 章 19.4；tests text_paint 断言像素一致）：
 * 字符格 8s×8s、行高 10s，s = max(1, lround(textSize/8))（textSize<=0 按 15sp
 * 默认折算）；measureText = 最长行字符数 × 8s（'\n' 换行）；baseline 在格顶下 8s，
 * ascent = -8s、descent = 2s、top = -8s、bottom = 2s、leading = 0。
 * 该口径与 framework/render.cc TextScale/TextMeasure* 及 Canvas.DrawText
 * 光栅化逐像素一致（text_paint_tests.sh 断言锚点）。
 *
 * <p>字段态（setColor/setTextSize/setTypeface 等）= 进程内状态机；绘制类效果
 * （Shader/Xfermode/PathEffect/ColorFilter）存引用，光栅化语义后置。
 */
public class Paint {
    public static final int ANTI_ALIAS_FLAG = 0x01;
    public static final int FILTER_BITMAP_FLAG = 0x02;
    public static final int DITHER_FLAG = 0x04;
    public static final int UNDERLINE_TEXT_FLAG = 0x08;
    public static final int STRIKE_THRU_TEXT_FLAG = 0x10;
    public static final int FAKE_BOLD_TEXT_FLAG = 0x20;
    public static final int LINEAR_TEXT_FLAG = 0x40;
    public static final int SUBPIXEL_TEXT_FLAG = 0x80;
    public static final int DEV_KERN_TEXT_FLAG = 0x100;
    public static final int LCD_RENDER_TEXT_FLAG = 0x200;
    public static final int EMBEDDED_BITMAP_TEXT_FLAG = 0x400;
    public static final int HINTING_OFF = 0x00;
    public static final int HINTING_ON = 0x01;

    public enum Style {
        FILL,
        STROKE,
        FILL_AND_STROKE
    }

    public enum Cap {
        BUTT,
        ROUND,
        SQUARE
    }

    public enum Join {
        MITER,
        ROUND,
        BEVEL
    }

    public enum Align {
        LEFT,
        CENTER,
        RIGHT
    }

    /** 字体度量（AOSP Paint.FontMetrics 公开字段态）。 */
    public static class FontMetrics {
        public float top;
        public float ascent;
        public float descent;
        public float bottom;
        public float leading;
    }

    /** 整型字体度量（AOSP Paint.FontMetricsInt 公开字段态）。 */
    public static class FontMetricsInt {
        public int top;
        public int ascent;
        public int descent;
        public int bottom;
        public int leading;
    }

    // ---- 字段态 ----
    int mColor = 0xFF000000;
    int mAlpha = 0xFF;
    float mTextSize = 15f;
    float mTextScaleX = 1f;
    float mTextSkewX = 0f;
    float mLetterSpacing = 0f;
    int mFlags = 0;
    int mHinting = HINTING_ON;
    Style mStyle = Style.FILL;
    Cap mStrokeCap = Cap.BUTT;
    Join mStrokeJoin = Join.MITER;
    float mStrokeWidth = 0f;
    float mStrokeMiter = 4f;
    Align mTextAlign = Align.LEFT;
    Typeface mTypeface;
    Locale mTextLocale = Locale.getDefault();
    LocaleList mTextLocales = new LocaleList(Locale.getDefault());
    ColorFilter mColorFilter;
    Shader mShader;
    Xfermode mXfermode;
    PathEffect mPathEffect;
    BlendMode mBlendMode;
    String mFontFeatureSettings;
    String mFontVariationSettings;
    boolean mElegantTextHeight;
    boolean mFilterBitmap;
    boolean mFakeBoldText;
    float mShadowRadius;
    float mShadowDx;
    float mShadowDy;
    int mShadowColor;

    public Paint() {
    }

    public Paint(int flags) {
        setFlags(flags);
    }

    public Paint(Paint paint) {
        set(paint);
    }

    public void set(Paint paint) {
        if (paint == null) return;
        this.mColor = paint.mColor;
        this.mAlpha = paint.mAlpha;
        this.mTextSize = paint.mTextSize;
        this.mTextScaleX = paint.mTextScaleX;
        this.mTextSkewX = paint.mTextSkewX;
        this.mLetterSpacing = paint.mLetterSpacing;
        this.mFlags = paint.mFlags;
        this.mHinting = paint.mHinting;
        this.mStyle = paint.mStyle;
        this.mStrokeCap = paint.mStrokeCap;
        this.mStrokeJoin = paint.mStrokeJoin;
        this.mStrokeWidth = paint.mStrokeWidth;
        this.mStrokeMiter = paint.mStrokeMiter;
        this.mTextAlign = paint.mTextAlign;
        this.mTypeface = paint.mTypeface;
        this.mTextLocale = paint.mTextLocale;
        this.mTextLocales = paint.mTextLocales;
        this.mColorFilter = paint.mColorFilter;
        this.mShader = paint.mShader;
        this.mXfermode = paint.mXfermode;
        this.mPathEffect = paint.mPathEffect;
        this.mBlendMode = paint.mBlendMode;
        this.mFontFeatureSettings = paint.mFontFeatureSettings;
        this.mFontVariationSettings = paint.mFontVariationSettings;
        this.mElegantTextHeight = paint.mElegantTextHeight;
        this.mFilterBitmap = paint.mFilterBitmap;
        this.mFakeBoldText = paint.mFakeBoldText;
        this.mShadowRadius = paint.mShadowRadius;
        this.mShadowDx = paint.mShadowDx;
        this.mShadowDy = paint.mShadowDy;
        this.mShadowColor = paint.mShadowColor;
    }

    public void reset() {
        set(new Paint());
    }

    // ---- 颜色面 ----
    public void setColor(int color) {
        this.mColor = color;
        this.mAlpha = (color >>> 24);
    }

    public int getColor() {
        return this.mColor;
    }

    public void setARGB(int a, int r, int g, int b) {
        setColor((a << 24) | (r << 16) | (g << 8) | b);
    }

    public void setAlpha(int a) {
        this.mAlpha = a & 0xFF;
        this.mColor = (this.mColor & 0x00FFFFFF) | (this.mAlpha << 24);
    }

    public int getAlpha() {
        return this.mAlpha;
    }

    // ---- 标志面 ----
    public void setFlags(int flags) {
        this.mFlags = flags;
        setAntiAlias((flags & ANTI_ALIAS_FLAG) != 0);
        setFilterBitmap((flags & FILTER_BITMAP_FLAG) != 0);
        setFakeBoldText((flags & FAKE_BOLD_TEXT_FLAG) != 0);
    }

    public int getFlags() {
        return this.mFlags;
    }

    public void setAntiAlias(boolean aa) {
        this.mFlags = aa ? (this.mFlags | ANTI_ALIAS_FLAG) : (this.mFlags & ~ANTI_ALIAS_FLAG);
    }

    public boolean isAntiAlias() {
        return (this.mFlags & ANTI_ALIAS_FLAG) != 0;
    }

    public void setFilterBitmap(boolean filter) {
        this.mFilterBitmap = filter;
    }

    public boolean isFilterBitmap() {
        return this.mFilterBitmap;
    }

    public void setFakeBoldText(boolean fakeBoldText) {
        this.mFakeBoldText = fakeBoldText;
    }

    public boolean isFakeBoldText() {
        return this.mFakeBoldText;
    }

    public void setHinting(int mode) {
        this.mHinting = mode;
    }

    public int getHinting() {
        return this.mHinting;
    }

    public boolean isDither() {
        return (this.mFlags & DITHER_FLAG) != 0;
    }

    public void setDither(boolean dither) {
        this.mFlags = dither ? (this.mFlags | DITHER_FLAG) : (this.mFlags & ~DITHER_FLAG);
    }

    // ---- 样式/描边面 ----
    public void setStyle(Style style) {
        this.mStyle = style != null ? style : Style.FILL;
    }

    public Style getStyle() {
        return this.mStyle;
    }

    public void setStrokeWidth(float width) {
        this.mStrokeWidth = width;
    }

    public float getStrokeWidth() {
        return this.mStrokeWidth;
    }

    public void setStrokeCap(Cap cap) {
        this.mStrokeCap = cap != null ? cap : Cap.BUTT;
    }

    public Cap getStrokeCap() {
        return this.mStrokeCap;
    }

    public void setStrokeJoin(Join join) {
        this.mStrokeJoin = join != null ? join : Join.MITER;
    }

    public Join getStrokeJoin() {
        return this.mStrokeJoin;
    }

    public void setStrokeMiter(float miter) {
        this.mStrokeMiter = miter;
    }

    public float getStrokeMiter() {
        return this.mStrokeMiter;
    }

    public boolean getFillPath(Path src, Path dst) {
        if (src == null || dst == null) return false;
        dst.set(src);
        return true;
    }

    // ---- 文本样式面 ----
    public Typeface setTypeface(Typeface typeface) {
        Typeface old = this.mTypeface;
        this.mTypeface = typeface;
        return old;
    }

    public Typeface getTypeface() {
        return this.mTypeface;
    }

    public void setTextSize(float textSize) {
        this.mTextSize = textSize;
    }

    public float getTextSize() {
        return this.mTextSize;
    }

    public void setTextScaleX(float scaleX) {
        this.mTextScaleX = scaleX;
    }

    public float getTextScaleX() {
        return this.mTextScaleX;
    }

    public void setTextSkewX(float skewX) {
        this.mTextSkewX = skewX;
    }

    public float getTextSkewX() {
        return this.mTextSkewX;
    }

    public void setTextAlign(Align align) {
        this.mTextAlign = align != null ? align : Align.LEFT;
    }

    public Align getTextAlign() {
        return this.mTextAlign;
    }

    public void setTextLocale(Locale locale) {
        this.mTextLocale = locale;
        if (locale != null) {
            this.mTextLocales = new LocaleList(locale);
        }
    }

    public Locale getTextLocale() {
        return this.mTextLocale;
    }

    public void setTextLocales(LocaleList locales) {
        this.mTextLocales = locales;
        this.mTextLocale = locales != null ? locales.get(0) : null;
    }

    public LocaleList getTextLocales() {
        return this.mTextLocales;
    }

    public void setLetterSpacing(float letterSpacing) {
        this.mLetterSpacing = letterSpacing;
    }

    public float getLetterSpacing() {
        return this.mLetterSpacing;
    }

    public void setElegantTextHeight(boolean elegant) {
        this.mElegantTextHeight = elegant;
    }

    public boolean isElegantTextHeight() {
        return this.mElegantTextHeight;
    }

    public void setFontFeatureSettings(String settings) {
        this.mFontFeatureSettings = settings;
    }

    public String getFontFeatureSettings() {
        return this.mFontFeatureSettings;
    }

    public void setFontVariationSettings(String settings) {
        this.mFontVariationSettings = settings;
    }

    public String getFontVariationSettings() {
        return this.mFontVariationSettings;
    }

    public boolean hasGlyph(String string) {
        return string != null && string.length() > 0;
    }

    // ---- 效果面（引用态；光栅化语义后置）----
    public ColorFilter setColorFilter(ColorFilter filter) {
        ColorFilter old = this.mColorFilter;
        this.mColorFilter = filter;
        return old;
    }

    public ColorFilter getColorFilter() {
        return this.mColorFilter;
    }

    public Shader setShader(Shader shader) {
        Shader old = this.mShader;
        this.mShader = shader;
        return old;
    }

    public Shader getShader() {
        return this.mShader;
    }

    public Xfermode setXfermode(Xfermode xfermode) {
        Xfermode old = this.mXfermode;
        this.mXfermode = xfermode;
        return old;
    }

    public Xfermode getXfermode() {
        return this.mXfermode;
    }

    public PathEffect setPathEffect(PathEffect effect) {
        PathEffect old = this.mPathEffect;
        this.mPathEffect = effect;
        return old;
    }

    public PathEffect getPathEffect() {
        return this.mPathEffect;
    }

    public void setBlendMode(BlendMode blendMode) {
        this.mBlendMode = blendMode;
    }

    public BlendMode getBlendMode() {
        return this.mBlendMode;
    }

    public void setShadowLayer(float radius, float dx, float dy, int shadowColor) {
        this.mShadowRadius = radius;
        this.mShadowDx = dx;
        this.mShadowDy = dy;
        this.mShadowColor = shadowColor;
    }

    public void clearShadowLayer() {
        this.mShadowRadius = 0;
        this.mShadowDx = 0;
        this.mShadowDy = 0;
        this.mShadowColor = 0;
    }

    // ---- 测量缝（render 管线网格口径；见类注释）----
    /** 网格缩放 s = max(1, lround(textSize/8))；textSize<=0 按 15sp 折算。 */
    private int gridScale() {
        float ts = this.mTextSize > 0 ? this.mTextSize : 15.0f;
        int s = Math.round(ts / 8.0f);
        return s < 1 ? 1 : s;
    }

    /** 最长行字符数（'\n' 换行；与 render TextMeasureWidth 同口径）。 */
    private static int maxLineChars(CharSequence text, int start, int end) {
        if (text == null || start >= end) return 0;
        int maxLen = 0;
        int cur = 0;
        for (int i = start; i < end; i++) {
            if (text.charAt(i) == '\n') {
                if (cur > maxLen) maxLen = cur;
                cur = 0;
            } else {
                cur++;
            }
        }
        return Math.max(maxLen, cur);
    }

    private static int lineCount(CharSequence text, int start, int end) {
        if (text == null || start >= end) return 1;
        int lines = 1;
        for (int i = start; i < end; i++) {
            if (text.charAt(i) == '\n') lines++;
        }
        return lines;
    }

    public float measureText(String text) {
        return measureText(text, 0, text == null ? 0 : text.length());
    }

    public float measureText(String text, int start, int end) {
        return measureText((CharSequence) text, start, end);
    }

    public float measureText(CharSequence text, int start, int end) {
        if (text == null) return 0f;
        if (start < 0) start = 0;
        if (end > text.length()) end = text.length();
        // 宿主真字体栈优先（AOSP：shaping/measure 在平台 native）；桥不可用
        // 或失败（-1）时回落网格口径。
        if (VmFonts.available()) {
            float w = VmFonts.measureText(fontHandle(),
                    text.subSequence(start, end).toString(), this.mTextSize);
            if (w >= 0f) return w * this.mTextScaleX;
        }
        return maxLineChars(text, start, end) * 8 * gridScale() * this.mTextScaleX;
    }

    private long fontHandle() {
        return this.mTypeface != null ? this.mTypeface.nativeHandle() : 0L;
    }

    public float measureText(char[] text, int index, int count) {
        if (text == null) return 0f;
        return measureText(new String(text, index, count), 0, count);
    }

    public float measureText(String text, int start, int end, float[] measuredWidth) {
        float w = measureText((CharSequence) text, start, end);
        if (measuredWidth != null && measuredWidth.length > 0) {
            measuredWidth[0] = w;
        }
        return w;
    }

    /** 宿主字体度量优先；桥不可用时回落网格（行高 10s / 单行 8s）。 */
    public int getFontMetricsInt(FontMetricsInt fmi) {
        int top;
        int descent;
        int ascent;
        int bottom;
        int leading;
        int[] out = new int[5];
        if (VmFonts.available() && VmFonts.fontMetrics(fontHandle(), this.mTextSize, out)) {
            top = out[0];
            ascent = out[1];
            descent = out[2];
            bottom = out[3];
            leading = out[4];
        } else {
            int s = gridScale();
            top = -8 * s;
            ascent = top;
            descent = 2 * s;
            bottom = descent;
            leading = 0;
        }
        if (fmi != null) {
            fmi.top = top;
            fmi.ascent = ascent;
            fmi.descent = descent;
            fmi.bottom = bottom;
            fmi.leading = leading;
        }
        return descent - top;
    }

    public FontMetricsInt getFontMetricsInt() {
        FontMetricsInt fmi = new FontMetricsInt();
        getFontMetricsInt(fmi);
        return fmi;
    }

    public float getFontMetrics(FontMetrics fm) {
        int top;
        int ascent;
        int descent;
        int bottom;
        int leading;
        int[] out = new int[5];
        if (VmFonts.available() && VmFonts.fontMetrics(fontHandle(), this.mTextSize, out)) {
            top = out[0];
            ascent = out[1];
            descent = out[2];
            bottom = out[3];
            leading = out[4];
        } else {
            int s = gridScale();
            top = -8 * s;
            ascent = top;
            descent = 2 * s;
            bottom = descent;
            leading = 0;
        }
        if (fm != null) {
            fm.top = top;
            fm.ascent = ascent;
            fm.descent = descent;
            fm.bottom = bottom;
            fm.leading = leading;
        }
        return descent - top;
    }

    public FontMetrics getFontMetrics() {
        FontMetrics fm = new FontMetrics();
        getFontMetrics(fm);
        return fm;
    }

    public float ascent() {
        return -8f * gridScale();
    }

    public float descent() {
        return 2f * gridScale();
    }

    public float getFontSpacing() {
        return 10f * gridScale();
    }

    public void getTextBounds(String text, int start, int end, Rect bounds) {
        getTextBounds((CharSequence) text, start, end, bounds);
    }

    public void getTextBounds(CharSequence text, int start, int end, Rect bounds) {
        if (bounds == null) return;
        if (text == null || start >= end) {
            bounds.setEmpty();
            return;
        }
        int s = gridScale();
        int w = Math.round(measureText(text, start, end));
        int lines = lineCount(text, start, end);
        bounds.set(0, -8 * s, w, (lines - 1) * 10 * s + 2 * s);
    }

    public void getTextBounds(char[] text, int index, int count, Rect bounds) {
        if (text == null) {
            if (bounds != null) bounds.setEmpty();
            return;
        }
        getTextBounds(new String(text, index, count), 0, count, bounds);
    }

    public float[] getTextWidths(String text, float[] widths) {
        return getTextWidths(text, 0, text == null ? 0 : text.length(), widths);
    }

    public float[] getTextWidths(String text, int start, int end, float[] widths) {
        if (text == null) return new float[0];
        int n = Math.max(0, end - start);
        if (widths == null || widths.length < n) {
            widths = new float[n];
        }
        int cell = 8 * gridScale();
        for (int i = 0; i < n; i++) {
            widths[i] = text.charAt(start + i) == '\n' ? 0 : cell;
        }
        return widths;
    }

    public float[] getTextWidths(CharSequence text, int start, int end, float[] widths) {
        return getTextWidths(text.toString(), start, end, widths);
    }

    /** 断字：在宽度内可容纳字符数（网格口径；measuredWidth 可空）。 */
    public int breakText(String text, boolean measureForwards, float maxWidth, float[] measuredWidth) {
        return breakText((CharSequence) text, 0, text == null ? 0 : text.length(),
                measureForwards, maxWidth, measuredWidth);
    }

    public int breakText(CharSequence text, int start, int end, boolean measureForwards,
                         float maxWidth, float[] measuredWidth) {
        if (text == null) return 0;
        int cell = Math.round(8 * gridScale() * this.mTextScaleX);
        if (cell <= 0) cell = 1;
        int maxChars = (int) (maxWidth / cell);
        int avail = Math.max(0, end - start);
        int n = Math.min(maxChars, avail);
        if (measuredWidth != null && measuredWidth.length > 0) {
            measuredWidth[0] = n * cell;
        }
        return n;
    }

    public int breakText(char[] text, int index, int count, float maxWidth, float[] measuredWidth) {
        if (text == null) return 0;
        return breakText(new String(text, index, count), true, maxWidth, measuredWidth);
    }
}
