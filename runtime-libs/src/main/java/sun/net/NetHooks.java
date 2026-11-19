package sun.net;

// 生成补源（sun-compat）：sun.net.NetHooks（socket 创建钩子面）。
public class NetHooks {
    public static final int SOCK_STREAM = 1;
    public static final int SOCK_DGRAM = 2;

    public static void beforeTcpCreate() {
    }

    public static void afterTcpCreate(java.net.SocketImpl impl) {
    }

    public static void beforeUdpCreate() {
    }

    public static void afterUdpCreate(java.net.SocketImpl impl) {
    }

    public static void beforeTcpConnect(java.io.FileDescriptor fd,
                                        java.net.InetAddress address, int port) {
    }

    public static void beforeTcpBind(java.io.FileDescriptor fd,
                                     java.net.InetAddress address, int port) {
    }

}
