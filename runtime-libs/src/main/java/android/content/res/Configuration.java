package android.content.res;

import android.os.Parcel;
import android.os.Parcelable;

import java.util.Locale;

/**
 * 设备配置（AOSP android.content.res.Configuration 公开字段态；§31.7 C 档 lib
 * POJO）。常量/字段与 AOSP 同名同型（业务码直读字段面）。
 */
public class Configuration implements Parcelable, Comparable<Configuration> {

    public static final int ORIENTATION_UNDEFINED = 0;
    public static final int ORIENTATION_PORTRAIT = 1;
    public static final int ORIENTATION_LANDSCAPE = 2;

    public static final int TOUCHSCREEN_UNDEFINED = 0;
    public static final int TOUCHSCREEN_NOTOUCH = 1;
    public static final int TOUCHSCREEN_STYLUS = 2;
    public static final int TOUCHSCREEN_FINGER = 3;

    public static final int KEYBOARD_UNDEFINED = 0;
    public static final int KEYBOARD_NOKEYS = 1;
    public static final int KEYBOARD_QWERTY = 2;
    public static final int KEYBOARD_12KEY = 3;

    public static final int NAVIGATION_UNDEFINED = 0;
    public static final int NAVIGATION_NONAV = 1;
    public static final int NAVIGATION_DPAD = 2;
    public static final int NAVIGATION_TRACKBALL = 3;
    public static final int NAVIGATION_WHEEL = 4;

    public static final int SCREENLAYOUT_SIZE_UNDEFINED = 0;
    public static final int SCREENLAYOUT_SIZE_SMALL = 1;
    public static final int SCREENLAYOUT_SIZE_NORMAL = 2;
    public static final int SCREENLAYOUT_SIZE_LARGE = 3;
    public static final int SCREENLAYOUT_SIZE_XLARGE = 4;

    public static final int UI_MODE_TYPE_NORMAL = 1;
    public static final int UI_MODE_NIGHT_UNDEFINED = 0;
    public static final int UI_MODE_NIGHT_NO = 1;
    public static final int UI_MODE_NIGHT_YES = 2;

    public static final int FONT_SCALE_UNDEFINED = 0;

    public static final int COLOR_MODE_UNDEFINED = 0;
    public static final int COLOR_MODE_WIDE_COLOR_GAMUT_UNDEFINED = 0;
    public static final int COLOR_MODE_WIDE_COLOR_GAMUT_NO = 1;
    public static final int COLOR_MODE_WIDE_COLOR_GAMUT_YES = 2;
    public static final int COLOR_MODE_HDR_UNDEFINED = 0;
    public static final int COLOR_MODE_HDR_NO = 1;
    public static final int COLOR_MODE_HDR_YES = 2;

    public float fontScale = 1.0f;
    public int mcc;
    public int mnc;
    public Locale locale = Locale.getDefault();
    public int orientation = ORIENTATION_UNDEFINED;
    public int touchscreen = TOUCHSCREEN_UNDEFINED;
    public int keyboard = KEYBOARD_UNDEFINED;
    public int keyboardHidden;
    public int hardKeyboardHidden = 1;
    public int navigation = NAVIGATION_UNDEFINED;
    public int navigationHidden;
    public int screenLayout;
    public int uiMode = UI_MODE_TYPE_NORMAL;
    public int screenWidthDp;
    public int screenHeightDp;
    public int smallestScreenWidthDp;
    /** AOSP 字段名即 densityDpi（旧 lib 曾误用 density）。 */
    public int densityDpi = 160;
    public float density = 1.0f;
    public int colorMode;
    public int seq;

    public Configuration() {
    }

    public Configuration(Configuration o) {
        if (o != null) setTo(o);
    }

    public void setTo(Configuration o) {
        if (o == null) return;
        this.fontScale = o.fontScale;
        this.mcc = o.mcc;
        this.mnc = o.mnc;
        this.locale = o.locale;
        this.orientation = o.orientation;
        this.touchscreen = o.touchscreen;
        this.keyboard = o.keyboard;
        this.keyboardHidden = o.keyboardHidden;
        this.navigation = o.navigation;
        this.navigationHidden = o.navigationHidden;
        this.screenLayout = o.screenLayout;
        this.uiMode = o.uiMode;
        this.screenWidthDp = o.screenWidthDp;
        this.screenHeightDp = o.screenHeightDp;
        this.smallestScreenWidthDp = o.smallestScreenWidthDp;
        this.density = o.density;
    }

    public void setToDefaults() {
        setTo(new Configuration());
    }

    public void updateFrom(Configuration delta) {
        if (delta == null) return;
        setTo(delta);
    }

    public int diff(Configuration delta) {
        return equals(delta) ? 0 : 1;
    }

    public boolean isLayoutSizeAtLeast(int size) {
        int cur = this.screenLayout & 0x0f;
        return cur >= size && cur != SCREENLAYOUT_SIZE_UNDEFINED;
    }

    /** AOSP 类型重载 equals(Configuration)（AppCompat/兼容链调用面） */
    public boolean equals(Configuration that) {
        return that != null && equals((Object) that);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Configuration)) return false;
        Configuration c = (Configuration) o;
        return this.fontScale == c.fontScale && this.mcc == c.mcc && this.mnc == c.mnc
                && this.orientation == c.orientation && this.uiMode == c.uiMode
                && this.screenWidthDp == c.screenWidthDp && this.screenHeightDp == c.screenHeightDp;
    }

    @Override
    public int hashCode() {
        return (int) this.fontScale * 31 + this.mcc * 17 + this.mnc + this.orientation;
    }

    @Override
    public int compareTo(Configuration that) {
        return 0;
    }

    @Override
    public String toString() {
        return "Configuration{" + this.screenWidthDp + "x" + this.screenHeightDp
                + " or=" + this.orientation + " loc=" + this.locale + "}";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }

    /** AOSP Configuration.getLocales()（AppCompat/资源配置链消费面）。 */
    public android.os.LocaleList getLocales() {
        Locale l = this.locale != null ? this.locale : Locale.getDefault();
        return new android.os.LocaleList(l);
    }

    public void setLocales(android.os.LocaleList locales) {
        if (locales != null && locales.size() > 0) {
            Locale first = locales.get(0);
            if (first != null) this.locale = first;
        }
    }

    public void setLocale(Locale loc) {
        if (loc != null) this.locale = loc;
    }

    /** AOSP：由 locale 推导布局方向（1=RTL，0=LTR；VM 以 Locale 文本面判定）。 */
    public int getLayoutDirection() {
        Locale l = this.locale != null ? this.locale : Locale.getDefault();
        String tag = l.getLanguage();
        if ("ar".equals(tag) || "fa".equals(tag) || "he".equals(tag) || "iw".equals(tag)
                || "ur".equals(tag) || "yi".equals(tag)) {
            return 1;
        }
        return 0;
    }
}
