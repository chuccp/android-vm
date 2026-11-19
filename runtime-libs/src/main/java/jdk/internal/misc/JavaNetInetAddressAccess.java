package jdk.internal.misc;

public interface JavaNetInetAddressAccess {
    String getOriginalHostName(java.net.InetAddress ia);
    java.net.InetAddress getByName(String hostName, java.net.InetAddress hostAddress)
            throws java.net.UnknownHostException;
}
