package javax.net.ssl;

import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;

/**
 * 桥接 SSLSocketFactory：每次 createSocket 向宿主 TLS 栈建立一条独立 TLS
 * 连接（不使用 guest 明文 socket fd；DNS/SNI/证书链/主机名校验由宿主完成）。
 */
public class BridgedSSLSocketFactory extends SSLSocketFactory {

    public BridgedSSLSocketFactory() {
    }

    @Override
    public Socket createSocket() {
        return new BridgedSSLSocket();
    }

    @Override
    public Socket createSocket(String host, int port)
            throws IOException, UnknownHostException {
        BridgedSSLSocket socket = new BridgedSSLSocket();
        socket.connectTls(host, port, 0);
        return socket;
    }

    @Override
    public Socket createSocket(String host, int port, InetAddress localHost, int localPort)
            throws IOException, UnknownHostException {
        // 本地绑定面不透传（宿主 TLS 栈代连）；目标语义保持一致。
        return createSocket(host, port);
    }

    @Override
    public Socket createSocket(InetAddress host, int port) throws IOException {
        return createSocket(addressName(host), port);
    }

    @Override
    public Socket createSocket(InetAddress address, int port, InetAddress localAddress,
                               int localPort) throws IOException {
        return createSocket(addressName(address), port);
    }

    @Override
    public Socket createSocket(Socket s, String host, int port, boolean autoClose)
            throws IOException {
        BridgedSSLSocket socket = new BridgedSSLSocket();
        try {
            socket.connectTls(host, port, 0);
        } catch (IOException e) {
            if (autoClose) closeQuietly(s);
            throw e;
        }
        // 原始 guest 明文连接不复用（宿主已对目标 host:port 新建 TLS）；autoClose
        // 语义 = SSLSocket 关闭时原 socket 亦关闭，这里直接释放避免空占连接。
        if (autoClose) closeQuietly(s);
        return socket;
    }

    @Override
    public String[] getDefaultCipherSuites() {
        return BridgedSSLSocket.defaultCipherSuites();
    }

    @Override
    public String[] getSupportedCipherSuites() {
        return BridgedSSLSocket.defaultCipherSuites();
    }

    private static String addressName(InetAddress address) {
        if (address == null) return null;
        // 不走反向 DNS（guest 网络栈阻塞面）：地址字面量作为 SNI 目标；
        // OkHttp 主路径经 createSocket(String,...) 提供真实主机名。
        return address.getHostAddress();
    }

    private static void closeQuietly(Socket socket) {
        if (socket == null) return;
        try {
            socket.close();
        } catch (Throwable ignored) {
        }
    }
}
