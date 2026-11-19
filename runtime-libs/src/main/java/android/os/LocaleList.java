package android.os;

import java.util.Arrays;
import java.util.Locale;

/**
 * Locale 列表（AOSP android.os.LocaleList 公开面；§31.7 C 档 lib POJO）。
 * Paint.getTextLocales 返回面载体；进程内数组语义。
 */
public final class LocaleList {
    public static final LocaleList sEmptyLocaleList = new LocaleList(new Locale[0]);

    private final Locale[] mList;

    public LocaleList(Locale... list) {
        if (list == null || list.length == 0) {
            this.mList = new Locale[0];
        } else {
            this.mList = list.clone();
        }
    }

    public static LocaleList getEmptyLocaleList() {
        return sEmptyLocaleList;
    }

    /** AOSP LocaleList.getDefault()：进程默认 Locale 列表（宿主导出面）。 */
    public static LocaleList getDefault() {
        return new LocaleList(Locale.getDefault());
    }

    /** AOSP LocaleList.getAdjustedDefault()（与 getDefault 同源；VM 单 Locale）。 */
    public static LocaleList getAdjustedDefault() {
        return getDefault();
    }

    public static LocaleList forLanguageTags(String list) {
        if (list == null || list.isEmpty()) return sEmptyLocaleList;
        String[] tags = list.split(",");
        Locale[] locales = new Locale[tags.length];
        for (int i = 0; i < tags.length; i++) {
            locales[i] = Locale.forLanguageTag(tags[i].trim());
        }
        return new LocaleList(locales);
    }

    public Locale get(int index) {
        if (index < 0 || index >= this.mList.length) return null;
        return this.mList[index];
    }

    public boolean isEmpty() {
        return this.mList.length == 0;
    }

    public int size() {
        return this.mList.length;
    }

    public int indexOf(Locale locale) {
        for (int i = 0; i < this.mList.length; i++) {
            if (this.mList[i].equals(locale)) return i;
        }
        return -1;
    }

    public String toLanguageTags() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < this.mList.length; i++) {
            if (i > 0) sb.append(',');
            sb.append(this.mList[i].toLanguageTag());
        }
        return sb.toString();
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof LocaleList)) return false;
        return Arrays.equals(this.mList, ((LocaleList) o).mList);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(this.mList);
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < this.mList.length; i++) {
            if (i > 0) sb.append(", ");
            sb.append(this.mList[i]);
        }
        return sb.append(']').toString();
    }
}
