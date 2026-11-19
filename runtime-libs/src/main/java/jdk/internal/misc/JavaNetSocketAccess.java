package jdk.internal.misc;

public interface JavaNetSocketAccess {
    java.net.ServerSocket newServerSocket(java.net.SocketImpl impl) throws java.io.IOException;
    java.net.SocketImpl newSocketImpl(Class<? extends java.net.SocketImpl> implClass)
            throws java.io.IOException;
}
