package jdk.internal.misc;

public interface JavaNetURLAccess {
    java.net.URLStreamHandler getHandler(java.net.URL u);
}
