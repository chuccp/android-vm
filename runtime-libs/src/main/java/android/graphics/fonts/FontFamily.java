package android.graphics.fonts;

import java.util.ArrayList;
import java.util.List;

/**
 * 字体族（AOSP android.graphics.fonts.FontFamily；§31.7 C 档 lib 骨架）。
 * 仅 Typeface.<init>(FontFamily) / TypefaceCompatApi29Impl 引用面载体；
 * 真字体解析后置。
 */
public final class FontFamily {

    private final List<Font> mFonts;

    public FontFamily() {
        this.mFonts = new ArrayList<>();
    }

    private FontFamily(List<Font> fonts) {
        this.mFonts = fonts;
    }

    public Font getFont(int index) {
        return index >= 0 && index < mFonts.size() ? mFonts.get(index) : null;
    }

    public Font getFirstFont() {
        return mFonts.isEmpty() ? null : mFonts.get(0);
    }

    public int getFontCount() {
        return mFonts.size();
    }

    /** AOSP FontFamily.Builder（TypefaceCompatApi29Impl 字体创建链）。 */
    public static final class Builder {
        private final List<Font> mFonts = new ArrayList<>();

        public Builder(Font font) {
            if (font != null) mFonts.add(font);
        }

        public Builder addFont(Font font) {
            if (font != null) mFonts.add(font);
            return this;
        }

        public Builder addFont(Font font, int weight, int slant) {
            if (font != null) mFonts.add(font);
            return this;
        }

        public FontFamily build() {
            return new FontFamily(new ArrayList<>(mFonts));
        }
    }
}
