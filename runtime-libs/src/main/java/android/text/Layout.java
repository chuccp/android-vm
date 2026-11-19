package android.text;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;

/**
 * 文本布局基类（AOSP android.text.Layout 公开面；§31.7 C 档 lib——复杂类禁
 * native，行折断纯 Java + Paint 测量缝）。
 *
 * <p>行模型（render 管线网格口径，docs 10 章 19.4）：行 i 占 y∈[i·10s,(i+1)·10s)，
 * baseline = i·10s+8s；行折断 = 贪心按字符格 8s×8s 折（'\n' 强制断行）。
 * draw(Canvas) 逐行 canvas.drawText（y=行顶，与 Canvas.DrawText 光栅化一致）。
 */
public class Layout {

    /** 对齐方式（AOSP Layout.Alignment 枚举）。 */
    public enum Alignment {
        ALIGN_NORMAL,
        ALIGN_OPPOSITE,
        ALIGN_CENTER,
        ALIGN_LEFT,
        ALIGN_RIGHT
    }

    static final int DIR_LEFT_TO_RIGHT = 1;
    static final int DIR_RIGHT_TO_LEFT = -1;

    final CharSequence mText;
    final TextPaint mPaint;
    final int mWidth;
    final Alignment mAlignment;
    final float mSpacingMult;
    final float mSpacingAdd;
    final boolean mIncludePad;
    final TextUtils$TruncateAt mEllipsize;
    final int mEllipsizedWidth;

    // 行折断结果（贪心；构造时算好）
    final int mLineCount;
    final int[] mLineStarts;   // 行起点（含），长度 = lineCount+1（末位 = text.length）
    final int[] mLineEnds;     // 行终点（不含）
    final int mLineHeight;     // 10s
    final int mCellWidth;      // 8s

    protected Layout(CharSequence source, TextPaint paint, int width, Alignment align,
                     float spacingMult, float spacingAdd, boolean includePad,
                     TextUtils$TruncateAt ellipsize, int ellipsizedWidth) {
        if (width < 0) {
            throw new IllegalArgumentException("Layout: negative width " + width);
        }
        this.mText = source != null ? source : "";
        this.mPaint = paint != null ? paint : new TextPaint();
        this.mWidth = width;
        this.mAlignment = align != null ? align : Alignment.ALIGN_NORMAL;
        this.mSpacingMult = spacingMult;
        this.mSpacingAdd = spacingAdd;
        this.mIncludePad = includePad;
        this.mEllipsize = ellipsize;
        this.mEllipsizedWidth = ellipsize != null ? ellipsizedWidth : width;

        int s = scale(this.mPaint);
        this.mCellWidth = 8 * s;
        this.mLineHeight = 10 * s;

        int[] breaks = breakLines(this.mText, this.mEllipsizedWidth > 0 ? this.mEllipsizedWidth
                : this.mWidth, this.mCellWidth);
        this.mLineStarts = breaks;
        this.mLineCount = breaks.length - 1;
        this.mLineEnds = new int[this.mLineCount];
        for (int i = 0; i < this.mLineCount; i++) {
            int end = breaks[i + 1];
            // 行尾 '\n' 不计入行内容（AOSP getLineEnd 不含换行符语义近似）
            if (end > breaks[i] && this.mText.charAt(end - 1) == '\n') {
                end--;
            }
            this.mLineEnds[i] = end;
        }
    }

    static int scale(TextPaint paint) {
        float ts = paint != null ? paint.getTextSize() : 0f;
        if (ts <= 0) ts = 15f;
        int s = Math.round(ts / 8f);
        return s < 1 ? 1 : s;
    }

    /** 贪心行折断（字符格粒度；'\n' 强制断行）。返回行起点数组（末位=text.length）。 */
    static int[] breakLines(CharSequence text, int width, int cellWidth) {
        int len = text.length();
        if (cellWidth <= 0) cellWidth = 1;
        int maxChars = width <= 0 ? 1 : Math.max(1, width / cellWidth);
        int[] tmp = new int[32];
        int n = 0;
        tmp[n++] = 0;
        int lineStart = 0;
        int col = 0;
        for (int i = 0; i < len; i++) {
            char ch = text.charAt(i);
            if (ch == '\n') {
                if (n == tmp.length) tmp = grow(tmp);
                tmp[n++] = i + 1;
                lineStart = i + 1;
                col = 0;
                continue;
            }
            col++;
            if (col > maxChars && i + 1 > lineStart) {
                // 在 i 处断行（i 归下一行）
                if (n == tmp.length) tmp = grow(tmp);
                tmp[n++] = i;
                lineStart = i;
                col = 1;
            }
        }
        if (n == tmp.length) tmp = grow(tmp);
        tmp[n++] = len;
        int[] out = new int[n];
        System.arraycopy(tmp, 0, out, 0, n);
        return out;
    }

    private static int[] grow(int[] a) {
        int[] b = new int[a.length * 2];
        System.arraycopy(a, 0, b, 0, a.length);
        return b;
    }

    // ---- 公开面（AOSP 形态）----

    public CharSequence getText() {
        return this.mText;
    }

    public TextPaint getPaint() {
        return this.mPaint;
    }

    public int getWidth() {
        return this.mWidth;
    }

    public int getEllipsizedWidth() {
        return this.mEllipsizedWidth;
    }

    public void increaseWidthTo(int wid) {
        // 宽度收窄语义后置（宽度即构造型；此处无状态可增）。
    }

    public int getHeight() {
        return getLineTop(this.mLineCount);
    }

    public int getLineCount() {
        return this.mLineCount;
    }

    public int getLineTop(int line) {
        return (int) (line * this.mLineHeight * this.mSpacingMult
                + line * this.mSpacingAdd);
    }

    public int getLineDescent(int line) {
        int s = this.mLineHeight / 10;
        return 2 * s;
    }

    public int getLineAscent(int line) {
        int s = this.mLineHeight / 10;
        return -8 * s;
    }

    public int getLineBaseline(int line) {
        return getLineTop(line) + 8 * (this.mLineHeight / 10);
    }

    public int getLineBounds(int line, Rect bounds) {
        int top = getLineTop(line);
        int bottom = getLineTop(line + 1);
        if (bounds != null) {
            bounds.set(0, top, getLineMax(line) > 0 ? (int) getLineMax(line) : 0, bottom);
        }
        return getLineBaseline(line);
    }

    public int getLineStart(int line) {
        if (line < 0) return 0;
        if (line >= this.mLineCount) return this.mText.length();
        return this.mLineStarts[line];
    }

    public int getLineEnd(int line) {
        if (line < 0) return 0;
        if (line >= this.mLineCount) return this.mText.length();
        return this.mLineEnds[line];
    }

    public int getLineVisibleEnd(int line) {
        return getLineEnd(line);
    }

    public float getLineWidth(int line) {
        return (getLineEnd(line) - getLineStart(line)) * this.mCellWidth;
    }

    /** 行最大绘制宽度（与 getLineWidth 同值；省略/制表符语义后置）。 */
    public float getLineMax(int line) {
        return getLineWidth(line);
    }

    public float getLineLeft(int line) {
        return getParagraphDirection(line) == DIR_RIGHT_TO_LEFT
                ? this.mWidth - getLineWidth(line) : 0;
    }

    public float getLineRight(int line) {
        return getParagraphDirection(line) == DIR_RIGHT_TO_LEFT
                ? this.mWidth : getLineWidth(line);
    }

    public int getParagraphDirection(int line) {
        return DIR_LEFT_TO_RIGHT;
    }

    public boolean isRtlCharAt(int off) {
        return false;
    }

    public int getLineForOffset(int offset) {
        int low = 0;
        int high = this.mLineCount - 1;
        while (low <= high) {
            int mid = (low + high) >> 1;
            int start = this.mLineStarts[mid];
            int end = this.mLineEnds[mid];
            if (offset < start) {
                high = mid - 1;
            } else if (offset >= end && mid < this.mLineCount - 1
                    && offset >= this.mLineStarts[mid + 1]) {
                low = mid + 1;
            } else {
                return mid;
            }
        }
        return this.mLineCount - 1 < 0 ? 0 : this.mLineCount - 1;
    }

    public int getLineForVertical(int vertical) {
        int line = 0;
        while (line < this.mLineCount - 1 && getLineTop(line + 1) <= vertical) {
            line++;
        }
        return line;
    }

    public float getHorizontal(int offset) {
        return getPrimaryHorizontal(offset);
    }

    public float getPrimaryHorizontal(int offset) {
        int line = getLineForOffset(offset);
        int col = offset - getLineStart(line);
        if (col < 0) col = 0;
        float x = col * this.mCellWidth;
        return getParagraphDirection(line) == DIR_RIGHT_TO_LEFT
                ? this.mWidth - x : x;
    }

    public float getSecondaryHorizontal(int offset) {
        return getPrimaryHorizontal(offset);
    }

    public float getParagraphLeft(int line) {
        return getLineLeft(line);
    }

    public float getParagraphRight(int line) {
        return getLineRight(line);
    }

    public int getOffsetForHorizontal(int line, float horiz) {
        int start = getLineStart(line);
        int end = getLineEnd(line);
        if (end <= start) return start;
        int col = (int) (Math.abs(horiz) / this.mCellWidth);
        if (col < 0) col = 0;
        if (col > end - start) col = end - start;
        return start + col;
    }

    public int getOffsetToLeftOf(int offset) {
        return offset > 0 ? offset - 1 : 0;
    }

    public int getOffsetToRightOf(int offset) {
        return offset + 1;
    }

    public int getEllipsisCount(int line) {
        return 0;
    }

    public int getEllipsisStart(int line) {
        return -1;
    }

    // ---- 绘制面 ----

    public void draw(Canvas canvas) {
        draw(canvas, null, null, 0);
    }

    public void draw(Canvas canvas, Path highlight, Paint highlightPaint, int cursorOffset) {
        if (canvas == null) return;
        for (int line = 0; line < this.mLineCount; line++) {
            int start = getLineStart(line);
            int end = getLineEnd(line);
            if (end > start) {
                String text = this.mText.subSequence(start, end).toString();
                canvas.drawText(text, getLineLeft(line), getLineTop(line), this.mPaint);
            }
        }
    }

    // ---- 静态测量面 ----

    /** 期望宽度（无折行单行最长字符数 × 格宽；AOSP getDesiredWidth 同形）。 */
    public static float getDesiredWidth(CharSequence source, TextPaint paint) {
        return getDesiredWidth(source, 0, source == null ? 0 : source.length(), paint);
    }

    public static float getDesiredWidth(CharSequence source, int start, int end,
                                        TextPaint paint) {
        if (source == null) return 0f;
        TextPaint p = paint != null ? paint : new TextPaint();
        int s = scale(p);
        int maxLen = 0;
        int cur = 0;
        for (int i = start; i < end && i < source.length(); i++) {
            if (source.charAt(i) == '\n') {
                if (cur > maxLen) maxLen = cur;
                cur = 0;
            } else {
                cur++;
            }
        }
        return Math.max(maxLen, cur) * 8 * s * p.getTextScaleX();
    }

    /** 段方向（LTR 恒定；复杂 BiDi 后置）。 */
    public int getParagraphDirectionAt(int line) {
        return getParagraphDirection(line);
    }
}
