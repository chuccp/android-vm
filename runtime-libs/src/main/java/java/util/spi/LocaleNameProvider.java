package java.util.spi;

import java.util.Locale;
import java.util.Objects;

public abstract class LocaleNameProvider extends LocaleServiceProvider {

    protected LocaleNameProvider() {
    }

    public abstract String getDisplayLanguage(String languageCode, Locale locale);

    public String getDisplayScript(String scriptCode, Locale locale) {
        return null;
    }

    public abstract String getDisplayCountry(String countryCode, Locale locale);

    public abstract String getDisplayVariant(String variant, Locale locale);

    public String getDisplayUnicodeExtensionKey(String key, Locale locale) {
        Objects.requireNonNull(key);
        Objects.requireNonNull(locale);
        return null;
    }

    public String getDisplayUnicodeExtensionType(String type, String key, Locale locale) {
        Objects.requireNonNull(type);
        Objects.requireNonNull(key);
        Objects.requireNonNull(locale);
        return null;
    }
}
