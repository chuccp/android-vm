package jdk.internal.misc;

public interface JavaUtilJarAccess {
    boolean jarFileHasClassPathAttribute(java.util.jar.JarFile jar) throws java.io.IOException;
    java.security.CodeSource[] getCodeSources(java.util.jar.JarFile jar, java.net.URL url);
    java.security.CodeSource getCodeSource(java.util.jar.JarFile jar, java.net.URL url, String name);
    java.util.Enumeration<String> entryNames(java.util.jar.JarFile jar,
                                             java.security.CodeSource[] cs);
    java.util.Enumeration<java.util.jar.JarEntry> entries2(java.util.jar.JarFile jar);
    void setEagerValidation(java.util.jar.JarFile jar, boolean eager);
    java.util.List<Object> getManifestDigests(java.util.jar.JarFile jar);
    java.util.jar.Attributes getTrustedAttributes(java.util.jar.Manifest man, String name);
    void ensureInitialization(java.util.jar.JarFile jar);
    boolean isInitializing();
}
