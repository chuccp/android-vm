package sun.util.locale;

// 生成补源（sun-compat）：JBR 25 的 sun.util.locale.BaseLocale 收缩；vendored
// Locale/ResourceBundle 以 BaseLocale 为 locale 核心键。OpenJDK 11 语义子集。
public final class BaseLocale {
    private static final BaseLocale EMPTY = new BaseLocale("", "", "", "");
    private static final java.util.concurrent.ConcurrentHashMap<String, BaseLocale> CACHE =
        new java.util.concurrent.ConcurrentHashMap<>();

    private final String language;
    private final String script;
    private final String region;
    private final String variant;

    private BaseLocale(String language, String script, String region, String variant) {
        this.language = language == null ? "" : language;
        this.script = script == null ? "" : script;
        this.region = region == null ? "" : region;
        this.variant = variant == null ? "" : variant;
    }

    public static BaseLocale getInstance(String language, String script,
                                         String region, String variant) {
        if (language == null) language = "";
        if (script == null) script = "";
        if (region == null) region = "";
        if (variant == null) variant = "";
        if (language.isEmpty() && script.isEmpty() && region.isEmpty() && variant.isEmpty()) {
            return EMPTY;
        }
        String key = language + "\u0000" + script + "\u0000" + region + "\u0000" + variant;
        final String lang = language;
        final String scr = script;
        final String reg = region;
        final String var = variant;
        return CACHE.computeIfAbsent(key,
            k -> new BaseLocale(lang, scr, reg, var));
    }

    public String getLanguage() {
        return language;
    }

    public String getScript() {
        return script;
    }

    public String getRegion() {
        return region;
    }

    public String getVariant() {
        return variant;
    }

    @Override
    public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof BaseLocale)) return false;
        BaseLocale o = (BaseLocale) other;
        return language.equals(o.language) && script.equals(o.script)
            && region.equals(o.region) && variant.equals(o.variant);
    }

    @Override
    public int hashCode() {
        return language.hashCode() ^ script.hashCode()
            ^ region.hashCode() ^ variant.hashCode();
    }

    @Override
    public String toString() {
        return "BaseLocale[" + language + ", " + script + ", "
            + region + ", " + variant + "]";
    }
}
