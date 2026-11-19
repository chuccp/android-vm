package android.graphics;

import android.content.res.AssetManager;
import android.graphics.fonts.FontFamily;
import android.graphics.fonts.FontStyle;

import java.io.File;
import java.io.FileDescriptor;

/**
 * 字体（AOSP android.graphics.Typeface 公开面；§31.7 C 档 lib）。
 *
 * <p>§31.7 判定：复杂类（字族解析/回退链）→ lib；本实现为「身份 + 风格态」
 * 形态——create/createFromAsset/createFromFile 返回风格化 Typeface 实例
 * （字体文件解析后置，不阻塞引用面）。默认字族 DEFAULT/SANS_SERIF/SERIF/
 * MONOSPACE 于 &lt;clinit&gt; 就绪。
 */
public class Typeface {
    public static final int NORMAL = 0;
    public static final int BOLD = 1;
    public static final int ITALIC = 2;
    public static final int BOLD_ITALIC = 3;

    private static Typeface sDefaultTypeface;

    public static final Typeface DEFAULT = new Typeface();
    public static final Typeface DEFAULT_BOLD = create(DEFAULT, BOLD);
    public static final Typeface SANS_SERIF = new Typeface();
    public static final Typeface SERIF = new Typeface();
    public static final Typeface MONOSPACE = new Typeface();

    static {
        sDefaultTypeface = DEFAULT;
    }

    private final int mStyle;

    public Typeface() {
        this(0);
    }

    private Typeface(int style) {
        this.mStyle = style;
    }

    public Typeface(AssetManager assetMgr, String path) {
        this(0);
    }

    public Typeface(FileDescriptor fd) {
        this(0);
    }

    public Typeface(FontFamily family) {
        this(0);
    }

    /** 家族内风格化创建（AOSP create(Typeface,int) 语义：风格参数，非回退）。 */
    public static Typeface create(Typeface family, int style) {
        int s = style & BOLD_ITALIC;
        if (family == null) return new Typeface(s);
        if (family.mStyle == s) return family;
        return new Typeface(s);
    }

    public static Typeface create(Typeface family, int style, boolean isBold) {
        return create(family, style);
    }

    /** 按 familyName 创建（真字体解析后置：返回风格载体）。 */
    public static Typeface create(String familyName, int style) {
        return create((Typeface) null, style);
    }

    public static Typeface createFromAsset(AssetManager assetMgr, String path) {
        return new Typeface();
    }

    public static Typeface createFromFile(File file) {
        return new Typeface();
    }

    public static Typeface createFromFile(String path) {
        return new Typeface();
    }

    public static Typeface defaultFromStyle(int style) {
        switch (style & BOLD_ITALIC) {
            case BOLD:
                return DEFAULT_BOLD;
            case ITALIC:
            case BOLD_ITALIC:
                return create(DEFAULT, style);
            default:
                return DEFAULT;
        }
    }

    public int getStyle() {
        return this.mStyle;
    }

    public boolean isBold() {
        return (this.mStyle & BOLD) != 0;
    }

    public boolean isItalic() {
        return (this.mStyle & ITALIC) != 0;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Typeface)) return false;
        Typeface t = (Typeface) o;
        return this.mStyle == t.mStyle;
    }

    @Override
    public int hashCode() {
        return this.mStyle;
    }

    /** 字族构建器（AOSP Typeface.Builder 面；解析后置）。 */
    public static final class Builder {
        private int mWeight = FontStyle.FONT_WEIGHT_NORMAL;
        private boolean mItalic;

        public Builder(File file) {
        }

        public Builder(FileDescriptor fd) {
        }

        public Builder(String path) {
        }

        public Builder setWeight(int weight) {
            this.mWeight = weight;
            return this;
        }

        public Builder setItalic(boolean italic) {
            this.mItalic = italic;
            return this;
        }

        public Builder setFontVariationSettings(String fontVariationSettings) {
            return this;
        }

        public Typeface build() {
            int style = (this.mWeight >= FontStyle.FONT_WEIGHT_BOLD ? BOLD : 0)
                    | (this.mItalic ? ITALIC : 0);
            return create((Typeface) null, style);
        }
    }

    /** 自定义回退构建器（AOSP Typeface.CustomFallbackBuilder 面；解析后置）。 */
    public static final class CustomFallbackBuilder {
        private int mStyle;

        public CustomFallbackBuilder(FontFamily baseFont) {
        }

        public CustomFallbackBuilder setStyle(FontStyle fontStyle) {
            if (fontStyle != null) {
                this.mStyle = (fontStyle.getWeight() >= FontStyle.FONT_WEIGHT_BOLD ? BOLD : 0)
                        | (fontStyle.isItalic() ? ITALIC : 0);
            }
            return this;
        }

        public Typeface build() {
            return create((Typeface) null, this.mStyle);
        }
    }
}
