package android.graphics.fonts;

/**
 * 字体风格（AOSP android.graphics.fonts.FontStyle；§31.7 C 档 lib POJO 骨架）。
 * 仅 Typeface 字体族引用面载体；真字体解析后置。
 */
public final class FontStyle {
    public static final int FONT_WEIGHT_NORMAL = 400;
    public static final int FONT_WEIGHT_BOLD = 700;

    private final int mWeight;
    private final int mSlant;

    public FontStyle(int weight, int slant) {
        this.mWeight = weight;
        this.mSlant = slant;
    }

    public int getWeight() {
        return this.mWeight;
    }

    public int getSlant() {
        return this.mSlant;
    }

    public boolean isItalic() {
        return this.mSlant == 1;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof FontStyle)) return false;
        FontStyle f = (FontStyle) o;
        return this.mWeight == f.mWeight && this.mSlant == f.mSlant;
    }

    @Override
    public int hashCode() {
        return 31 * this.mWeight + this.mSlant;
    }

    @Override
    public String toString() {
        return "FontStyle(weight=" + this.mWeight + ", slant=" + this.mSlant + ")";
    }
}
