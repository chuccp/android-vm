package jdk.internal.misc;

public interface JavaUtilResourceBundleAccess {
    void setParent(java.util.ResourceBundle bundle, java.util.ResourceBundle parent);
    java.util.ResourceBundle getParent(java.util.ResourceBundle bundle);
    void setLocale(java.util.ResourceBundle bundle, java.util.Locale locale);
    void setName(java.util.ResourceBundle bundle, String name);
    java.util.ResourceBundle getBundle(String baseName, java.util.Locale locale,
                                       java.lang.Module module);
    java.util.ResourceBundle newResourceBundle(
            Class<? extends java.util.ResourceBundle> bundleClass)
            throws InstantiationException, IllegalAccessException;
}
