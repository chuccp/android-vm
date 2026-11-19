package android.graphics;

import android.content.res.AssetManager;
import android.graphics.fonts.Font;
import android.graphics.fonts.FontFamily;
import android.graphics.fonts.FontStyle;

import java.io.File;
import java.io.FileDescriptor;

/**
 * 字体（AOSP android.graphics.Typeface 公开面；§31.7 C 档 lib）。
 *
 * <p>字体解析/回退链由宿主真 Android 字体栈承接（VmFonts 桥 -> 宿主
 * FontBridge/Typeface）；本类保留 AOSP API 形状与风格态，句柄 0 = 平台默认
 * 字体。桥不可用（桌面单测）时句柄保持 0，Paint 侧回落网格度量。
 */
public class Typeface {
    public static final int NORMAL = 0;
    public static final int BOLD = 1;
    public static final int ITALIC = 2;
    public static final int BOLD_ITALIC = 3;

    private static Typeface sDefaultTypeface;

    public static final Typeface DEFAULT = new Typeface();
    public static final Typeface DEFAULT_BOLD = create(DEFAULT, BOLD);
    public static final Typeface SANS_SERIF = create("sans-serif", NORMAL);
    public static final Typeface SERIF = create("serif", NORMAL);
    public static final Typeface MONOSPACE = create("monospace", NORMAL);

    static {
        sDefaultTypeface = DEFAULT;
    }

    private final int mStyle;
    private final long mNativeHandle;

    public Typeface() {
        this(0, 0L);
    }

    private Typeface(int style, long nativeHandle) {
        this.mStyle = style;
        this.mNativeHandle = nativeHandle;
    }

    public Typeface(AssetManager assetMgr, String path) {
        this(0, 0L);
    }

    public Typeface(FileDescriptor fd) {
        this(0, 0L);
    }

    public Typeface(FontFamily family) {
        this(0, handleFromFamily(family, 0));
    }

    private static long handleFromFamily(FontFamily family, int style) {
        if (family == null || !VmFonts.available()) return 0L;
        Font first = family.getFirstFont();
        if (first != null && first.getResourceId() != 0) {
            long h = VmFonts.createFromResource(first.getResourceId(), style);
            if (h != 0) return h;
        }
        return VmFonts.createFromFamily(null, style);
    }

    /** 宿主字体句柄（0 = 平台默认字体；materializer/ViewModel 诊断面）。 */
    public long nativeHandle() {
        return this.mNativeHandle;
    }

    /** 家族内风格化创建（AOSP create(Typeface,int) 语义：风格参数，非回退）。 */
    public static Typeface create(Typeface family, int style) {
        int s = style & BOLD_ITALIC;
        long h = 0L;
        if (VmFonts.available()) {
            if (family != null && family.mNativeHandle != 0) {
                h = VmFonts.restyle(family.mNativeHandle, s);
            }
            if (h == 0L) h = VmFonts.createFromFamily(null, s);
        }
        if (family != null && family.mStyle == s && family.mNativeHandle == h) return family;
        return new Typeface(s, h);
    }

    public static Typeface create(Typeface family, int style, boolean isBold) {
        return create(family, style);
    }

    /** 按 familyName 创建（宿主 Typeface.create(family, style)）。 */
    public static Typeface create(String familyName, int style) {
        int s = style & BOLD_ITALIC;
        long h = VmFonts.available() ? VmFonts.createFromFamily(familyName, s) : 0L;
        return new Typeface(s, h);
    }

    public static Typeface createFromAsset(AssetManager assetMgr, String path) {
        // 资产字体暂按默认字体（APK 内资产需抽取落盘后交宿主）。
        return create((String) null, 0);
    }

    public static Typeface createFromFile(File file) {
        return file != null ? createFromFile(file.getAbsolutePath()) : create((String) null, 0);
    }

    public static Typeface createFromFile(String path) {
        long h = VmFonts.available() && path != null
                ? VmFonts.createFromFile(path, 0) : 0L;
        return new Typeface(0, h);
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
        return this.mStyle == t.mStyle && this.mNativeHandle == t.mNativeHandle;
    }

    @Override
    public int hashCode() {
        return this.mStyle * 31 + (int) (this.mNativeHandle ^ (this.mNativeHandle >>> 32));
    }

    /** 字族构建器（AOSP Typeface.Builder 面）。 */
    public static final class Builder {
        private int mWeight = FontStyle.FONT_WEIGHT_NORMAL;
        private boolean mItalic;
        private String mPath;

        public Builder(File file) {
            this.mPath = file != null ? file.getAbsolutePath() : null;
        }

        public Builder(FileDescriptor fd) {
        }

        public Builder(String path) {
            this.mPath = path;
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
            if (this.mPath != null && VmFonts.available()) {
                long h = VmFonts.createFromFile(this.mPath, style);
                if (h != 0) return new Typeface(style, h);
            }
            return create((String) null, style);
        }
    }

    /** 自定义回退构建器（AOSP Typeface.CustomFallbackBuilder 面）。 */
    public static final class CustomFallbackBuilder {
        private final FontFamily mFamily;
        private int mStyle;

        public CustomFallbackBuilder(FontFamily baseFont) {
            this.mFamily = baseFont;
        }

        public CustomFallbackBuilder setStyle(FontStyle fontStyle) {
            if (fontStyle != null) {
                this.mStyle = (fontStyle.getWeight() >= FontStyle.FONT_WEIGHT_BOLD ? BOLD : 0)
                        | (fontStyle.isItalic() ? ITALIC : 0);
            }
            return this;
        }

        public Typeface build() {
            long h = handleFromFamily(this.mFamily, this.mStyle);
            return new Typeface(this.mStyle, h);
        }
    }
}
