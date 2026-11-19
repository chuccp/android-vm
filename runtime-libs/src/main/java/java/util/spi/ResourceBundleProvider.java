package java.util.spi;

import java.util.Locale;
import java.util.ResourceBundle;

public interface ResourceBundleProvider {

    public ResourceBundle getBundle(String baseName, Locale locale);
}
