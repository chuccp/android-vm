package jdk.internal.misc;

public interface JavaNetUriAccess {
    java.net.URI create(String scheme, String path);
}
