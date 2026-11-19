package jdk.internal.misc;

public interface JavaNetHttpCookieAccess {
    java.util.List<java.net.HttpCookie> parse(String header);
    String header(java.net.HttpCookie cookie);
}
