package android.graphics.fonts;

import android.content.res.Resources;

import java.io.File;
import java.nio.ByteBuffer;

/**
 * android.graphics.fonts.Font（AOSP API 29 公开面骨架）。
 *
 * <p>真字体解析后置；Font.Builder 面齐备（创建路径返回风格态 Font 实例，
 * 供 Typeface/文本测量链的类型与风格查询消费）。
 */
public final class Font {
    public static final int SLANT_UPRIGHT = 0;
    public static final int SLANT_ITALIC = 1;

    private final int mWeight;
    private final int mSlant;
    private final int mTtcIndex;
    private final FontStyle mStyle;
    private final int mResourceId;

    private Font(int weight, int slant, int ttcIndex) {
        this(weight, slant, ttcIndex, 0);
    }

    private Font(int weight, int slant, int ttcIndex, int resourceId) {
        this.mWeight = weight;
        this.mSlant = slant;
        this.mTtcIndex = ttcIndex;
        this.mStyle = new FontStyle(weight, slant);
        this.mResourceId = resourceId;
    }

    /** APK font 资源 id（0 = 非资源字体；Typeface 桥据此交宿主解析）。 */
    public int getResourceId() {
        return mResourceId;
    }

    public FontStyle getStyle() {
        return mStyle;
    }

    public String getPostScriptName() {
        return "sans-serif";
    }

    public int getTtcIndex() {
        return mTtcIndex;
    }

    public int getWeight() {
        return mWeight;
    }

    public int getSlant() {
        return mSlant;
    }

    /** AOSP Font.Builder。 */
    public static final class Builder {
        private int mWeight = FontStyle.FONT_WEIGHT_NORMAL;
        private int mSlant = SLANT_UPRIGHT;
        private int mTtcIndex = 0;
        private int mResourceId = 0;

        public Builder(ByteBuffer buffer) {
        }

        public Builder(ByteBuffer buffer, File file, String ttcIndex) {
        }

        public Builder(File path) {
        }

        public Builder(File path, String ttcIndex) {
        }

        /** AOSP Font.Builder(Resources, int)：TypefaceCompatApi29Impl 资源字体链。 */
        public Builder(Resources res, int id) {
            mResourceId = id;
        }

        public Builder(Resources res, int id, int ttcIndex) {
            mResourceId = id;
            mTtcIndex = ttcIndex;
        }

        public Builder setWeight(int weight) {
            mWeight = weight;
            return this;
        }

        public Builder setSlant(int slant) {
            mSlant = slant;
            return this;
        }

        public Builder setTtcIndex(int ttcIndex) {
            mTtcIndex = ttcIndex;
            return this;
        }

        public Builder setFontVariationSettings(String variationSettings) {
            return this;
        }

        public Font build() {
            return new Font(mWeight, mSlant, mTtcIndex, mResourceId);
        }
    }
}
