package sun.net.util;

// 生成补源（sun-compat）：sun.net.SocketExceptions（异常文案面）。
public class SocketExceptions {
    public static String unboundSocket(String op) {
        return op + ": socket not bound";
    }

    public static String socketClosed(String op) {
        return op + ": socket closed";
    }

    public static java.net.SocketException of(java.io.IOException e,
                                              java.net.InetSocketAddress addr) {
        java.net.SocketException se = new java.net.SocketException(e.getMessage());
        se.initCause(e);
        return se;
    }

}
