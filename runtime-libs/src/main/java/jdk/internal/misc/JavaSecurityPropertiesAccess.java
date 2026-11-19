package jdk.internal.misc;

public interface JavaSecurityPropertiesAccess {
    java.util.Properties getInitialProperties();

    default void loadProperties(java.util.Properties props) {
    }

    default String getProperty(String key) {
        return null;
    }
}
