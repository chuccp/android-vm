package java.util;

public final class Locale {
    public static final Locale ROOT = new Locale("", "", "");
    public static final Locale US = new Locale("en", "US", "");
    public static final Locale CHINA = new Locale("zh", "CN", "");
    public static final Locale ENGLISH = new Locale("en", "", "");

    private static Locale defaultLocale = US;

    private final String language;
    private final String country;
    private final String variant;

    public Locale(String language, String country, String variant) {
        this.language = normalizeLanguage(language);
        this.country = normalizeCountry(country);
        this.variant = variant == null ? "" : variant;
    }

    public Locale(String language, String country) {
        this(language, country, "");
    }

    public Locale(String language) {
        this(language, "", "");
    }

    public static Locale getDefault(Category category) {
        return getDefault();
    }

    public static void setDefault(Category category, Locale locale) {
        setDefault(locale);
    }

    public String getDisplayName(Locale locale) {
        return getDisplayName();
    }

    public String getDisplayLanguage(Locale locale) {
        return getDisplayLanguage();
    }

    public String getDisplayCountry(Locale locale) {
        return getDisplayCountry();
    }

    public static Locale getDefault() {
        return defaultLocale;
    }

    public static void setDefault(Locale locale) {
        if (locale == null) throw new NullPointerException("locale");
        defaultLocale = locale;
    }

    public static Locale forLanguageTag(String tag) {
        if (tag == null) throw new NullPointerException("languageTag");
        String normalized = tag.replace('_', '-');
        String[] parts = normalized.split("-", -1);
        String language = parts.length > 0 ? parts[0] : "";
        String country = "";
        String variant = "";
        if (parts.length > 1) {
            if (parts[1].length() == 2 ||
                    parts[1].length() == 3) {
                country = parts[1];
            } else {
                variant = parts[1];
            }
        }
        if (parts.length > 2) {
            StringBuilder remaining = new StringBuilder();
            for (int index = 2; index < parts.length; index++) {
                if (remaining.length() > 0) remaining.append('_');
                remaining.append(parts[index]);
            }
            variant = remaining.toString();
        }
        return new Locale(language, country, variant);
    }

    public String getLanguage() {
        return language;
    }

    public String getCountry() {
        return country;
    }

    public String getVariant() {
        return variant;
    }

    public String toLanguageTag() {
        if (language.isEmpty()) return "und";
        return country.isEmpty() ? language : language + "-" + country;
    }

    public String getDisplayName() {
        return toString();
    }

    public String getDisplayLanguage() {
        return language;
    }

    public String getDisplayCountry() {
        return country;
    }

    @Override
    public String toString() {
        StringBuilder result = new StringBuilder(language);
        if (!country.isEmpty()) result.append('_').append(country);
        if (!variant.isEmpty()) result.append('_').append(variant);
        return result.length() == 0 ? "" : result.toString();
    }

    @Override
    public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof Locale)) return false;
        Locale locale = (Locale) other;
        return language.equals(locale.language) &&
            country.equals(locale.country) &&
            variant.equals(locale.variant);
    }

    @Override
    public int hashCode() {
        return javaHash(language) ^ javaHash(country) ^ javaHash(variant);
    }

    private static int javaHash(String value) {
        int result = 0;
        for (int index = 0; index < value.length(); index++) {
            result = 31 * result + value.charAt(index);
        }
        return result;
    }

    private static String normalizeLanguage(String value) {
        return normalize(value, false);
    }

    private static String normalizeCountry(String value) {
        return normalize(value, true);
    }

    private static String normalize(String value, boolean upper) {
        if (value == null) return "";
        StringBuilder output = new StringBuilder(value.length());
        for (int index = 0; index < value.length(); index++) {
            char character = value.charAt(index);
            if (upper && character >= 'a' && character <= 'z') {
                character = (char) (character - ('a' - 'A'));
            } else if (!upper && character >= 'A' && character <= 'Z') {
                character = (char) (character + ('a' - 'A'));
            }
            output.append(character);
        }
        return output.toString();
    }

    // ---- overlay：Category/getInstance/getScript/getBaseLocale/stripExtensions
    // API 补全（供 ResourceBundle/DateFormatSymbols/Scanner 等全量源编译）----
    public enum Category {
        DISPLAY(""),
        FORMAT("");

        private final String key;

        Category(String key) {
            this.key = key;
        }
    }

    public String getScript() {
        return "";
    }

    public String getUnicodeLocaleType(String key) {
        return null;
    }

    public Locale stripExtensions() {
        return this;
    }

    public sun.util.locale.BaseLocale getBaseLocale() {
        return sun.util.locale.BaseLocale.getInstance(language, "", country, variant);
    }

    public static Locale getInstance(String language, String country, String variant) {
        return new Locale(language, country, variant);
    }

    public static Locale getInstance(String language, String script, String country,
                                     String variant, java.util.Set<String> extensions) {
        return new Locale(language, country, variant);
    }

}
