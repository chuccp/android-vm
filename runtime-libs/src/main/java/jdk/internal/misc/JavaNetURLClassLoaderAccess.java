package jdk.internal.misc;

public interface JavaNetURLClassLoaderAccess {
    java.security.AccessControlContext getAccessControlContext(java.net.URLClassLoader u);
}
