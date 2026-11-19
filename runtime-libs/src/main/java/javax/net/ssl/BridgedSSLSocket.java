package javax.net.ssl;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketAddress;
import java.net.SocketException;
import java.security.cert.Certificate;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * guest SSLSocket 薄壳：字节流与生命周期经 {@link BridgedTls} 桥到宿主真 TLS
 * 栈。createSocket(Socket s, host, port, autoClose) 的原始 guest socket 不复用
 * （宿主对目标 host:port 新建 TLS 连接，DNS/SNI/证书校验全部宿主完成）；autoClose
 * 语义由 factory 在建立桥接后关闭原始 socket 承接。
 *
 * 握手在宿主 OPEN 内完成，startHandshake 为 no-op；SO_TIMEOUT 映射宿主读超时。
 */
final class BridgedSSLSocket extends SSLSocket {
    private static final String[] DEFAULT_PROTOCOLS = {"TLSv1.3", "TLSv1.2"};

    // OkHttp ConnectionSpec 兼容性判据（enabled/supported 交集非空即可）；宿主
    // 忽略 guest cipher 配置，真实协商结果以宿主为准。
    private static final String[] DEFAULT_CIPHER_SUITES = {
            "TLS_AES_128_GCM_SHA256",
            "TLS_AES_256_GCM_SHA384",
            "TLS_CHACHA20_POLY1305_SHA256",
            "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256",
            "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256",
            "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384",
            "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384",
            "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256",
            "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256",
            "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA",
            "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA",
            "TLS_RSA_WITH_AES_128_GCM_SHA256",
            "TLS_RSA_WITH_AES_256_GCM_SHA384",
            "TLS_RSA_WITH_AES_128_CBC_SHA",
            "TLS_RSA_WITH_AES_256_CBC_SHA",
            "TLS_EMPTY_RENEGOTIATION_INFO_SCSV",
    };

    private long handle;
    private boolean connected;
    private boolean closed;
    private boolean inputShutdown;
    private boolean outputShutdown;
    private int soTimeoutMs;
    private String host;
    private int port;
    private InetAddress peerAddress;
    private BridgedSSLSession session;
    private InputStream inputStream;
    private OutputStream outputStream;
    private final List<HandshakeCompletedListener> handshakeListeners = new ArrayList<>();
    private boolean useClientMode = true;
    private boolean needClientAuth;
    private boolean wantClientAuth;
    private boolean enableSessionCreation = true;
    private String[] enabledProtocols = DEFAULT_PROTOCOLS.clone();
    private String[] enabledCipherSuites = DEFAULT_CIPHER_SUITES.clone();

    BridgedSSLSocket() {
        super();
    }

    // ------------------------------------------------------------------
    // 宿主桥接连接
    // ------------------------------------------------------------------

    void connectTls(String host, int port, int connectTimeoutMs) throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        if (connected) throw new SocketException("already connected");
        String target = host == null ? "localhost" : host;
        String metadata = BridgedTls.open(target, port, connectTimeoutMs);
        parseMetadata(metadata, target, port);
        this.connected = true;
    }

    private void parseMetadata(String metadata, String target, int targetPort) throws IOException {
        if (metadata == null) throw new IOException("TLS bridge returned no metadata");
        Map<String, List<String>> lines = new HashMap<>();
        String[] rawLines = metadata.split("\n");
        for (String line : rawLines) {
            int eq = line.indexOf('=');
            if (eq <= 0) continue;
            String key = line.substring(0, eq);
            String value = line.substring(eq + 1);
            List<String> values = lines.get(key);
            if (values == null) {
                values = new ArrayList<>();
                lines.put(key, values);
            }
            values.add(value);
        }
        String handleText = first(lines, "handle");
        if (handleText == null) throw new IOException("TLS bridge metadata missing handle");
        try {
            handle = Long.parseLong(handleText.trim());
        } catch (NumberFormatException e) {
            throw new IOException("TLS bridge invalid handle: " + handleText);
        }
        if (handle <= 0) throw new IOException("TLS bridge invalid handle: " + handleText);

        this.host = target;
        this.port = targetPort;
        String addr = first(lines, "addr");
        if (addr != null && !addr.isEmpty()) {
            try {
                peerAddress = InetAddress.getByName(addr);
            } catch (Throwable ignored) {
                peerAddress = null;
            }
        }

        List<List<?>> sans = new ArrayList<>();
        List<String> sanLines = lines.get("san");
        if (sanLines != null) {
            for (String san : sanLines) {
                int bar = san.indexOf('|');
                if (bar <= 0) continue;
                try {
                    int type = Integer.parseInt(san.substring(0, bar));
                    List<Object> entry = new ArrayList<>(2);
                    entry.add(Integer.valueOf(type));
                    entry.add(san.substring(bar + 1));
                    sans.add(entry);
                } catch (NumberFormatException ignored) {
                }
            }
        }
        if (sans.isEmpty()) {
            // 宿主握手已做 HTTPS endpoint identification（真实校验）；此处在
            // 证书无 SAN 的极少数场景把目标身份投影给 guest 校验器。
            List<Object> entry = new ArrayList<>(2);
            entry.add(Integer.valueOf(isIpLiteral(target) ? 7 : 2));
            entry.add(target);
            sans.add(entry);
        }

        String keyAlgorithm = null;
        byte[] keyEncoded = null;
        String spki = first(lines, "spki");
        if (spki != null) {
            int bar = spki.indexOf('|');
            if (bar > 0) {
                keyAlgorithm = spki.substring(0, bar);
                keyEncoded = hexToBytes(spki.substring(bar + 1));
            }
        }

        BridgedX509Certificate leaf = new BridgedX509Certificate(
                first(lines, "subject"), first(lines, "issuer"), sans, keyAlgorithm, keyEncoded);
        String protocol = first(lines, "protocol");
        String cipher = first(lines, "cipher");
        this.session = new BridgedSSLSession(protocol, cipher, target, targetPort,
                new Certificate[] {leaf});
    }

    private static String first(Map<String, List<String>> lines, String key) {
        List<String> values = lines.get(key);
        return values == null || values.isEmpty() ? null : values.get(0);
    }

    private static boolean isIpLiteral(String value) {
        if (value == null) return false;
        if (value.indexOf(':') >= 0) return true;  // IPv6
        boolean digitsAndDots = value.length() > 0;
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if ((c < '0' || c > '9') && c != '.') {
                digitsAndDots = false;
                break;
            }
        }
        return digitsAndDots;
    }

    private static byte[] hexToBytes(String hex) {
        if (hex == null) return new byte[0];
        int n = hex.length() / 2;
        byte[] out = new byte[n];
        for (int i = 0; i < n; i++) {
            int hi = Character.digit(hex.charAt(i * 2), 16);
            int lo = Character.digit(hex.charAt(i * 2 + 1), 16);
            if (hi < 0 || lo < 0) return new byte[0];
            out[i] = (byte) ((hi << 4) | lo);
        }
        return out;
    }

    @Override
    public void connect(SocketAddress endpoint) throws IOException {
        connect(endpoint, 0);
    }

    @Override
    public void connect(SocketAddress endpoint, int timeout) throws IOException {
        if (!(endpoint instanceof InetSocketAddress)) {
            throw new SocketException("unsupported address type: " + endpoint);
        }
        InetSocketAddress address = (InetSocketAddress) endpoint;
        connectTls(address.getHostString(), address.getPort(), timeout);
    }

    private void ensureOpen() throws SocketException {
        if (closed) throw new SocketException("Socket is closed");
        if (!connected) throw new SocketException("Socket is not connected");
    }

    @Override
    public InputStream getInputStream() throws IOException {
        ensureOpen();
        if (inputShutdown) throw new SocketException("Socket input is shutdown");
        if (inputStream == null) inputStream = new BridgedInputStream();
        return inputStream;
    }

    @Override
    public OutputStream getOutputStream() throws IOException {
        ensureOpen();
        if (outputShutdown) throw new SocketException("Socket output is shutdown");
        if (outputStream == null) outputStream = new BridgedOutputStream();
        return outputStream;
    }

    @Override
    public synchronized void close() throws IOException {
        if (closed) return;
        closed = true;
        if (session != null) session.invalidate();
        long current = handle;
        handle = 0;
        if (current != 0) {
            // 宿主 close 失败不改变 guest 已关闭语义（句柄已释放）。
            try {
                BridgedTls.close(current);
            } catch (Throwable ignored) {
            }
        }
    }

    private final class BridgedInputStream extends InputStream {
        @Override
        public int read() throws IOException {
            byte[] data = read0(1);
            return data == null ? -1 : (data[0] & 0xFF);
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (b == null) throw new NullPointerException("buffer");
            if (off < 0 || len < 0 || off > b.length - len) {
                throw new IndexOutOfBoundsException();
            }
            if (len == 0) return 0;
            byte[] data = read0(len);
            if (data == null) return -1;
            System.arraycopy(data, 0, b, off, data.length);
            return data.length;
        }

        private byte[] read0(int len) throws IOException {
            ensureOpen();
            if (inputShutdown) return null;
            long current = handle;
            if (current == 0) return null;
            return BridgedTls.read(current, soTimeoutMs, len);
        }

        @Override
        public int available() {
            return 0;
        }

        @Override
        public void close() throws IOException {
            BridgedSSLSocket.this.close();
        }
    }

    private final class BridgedOutputStream extends OutputStream {
        @Override
        public void write(int b) throws IOException {
            write(new byte[] {(byte) b}, 0, 1);
        }

        @Override
        public void write(byte[] b, int off, int len) throws IOException {
            if (b == null) throw new NullPointerException("buffer");
            if (off < 0 || len < 0 || off > b.length - len) {
                throw new IndexOutOfBoundsException();
            }
            if (len == 0) return;
            ensureOpen();
            long current = handle;
            if (current == 0) throw new SocketException("Socket is closed");
            BridgedTls.write(current, b, off, len);
        }

        @Override
        public void flush() {
        }

        @Override
        public void close() throws IOException {
            BridgedSSLSocket.this.close();
        }
    }

    // ------------------------------------------------------------------
    // Socket 面（不触达 guest PlainSocketImpl）
    // ------------------------------------------------------------------

    @Override
    public InetAddress getInetAddress() {
        return peerAddress;
    }

    @Override
    public int getPort() {
        return connected ? port : 0;
    }

    @Override
    public InetAddress getLocalAddress() {
        return InetAddress.getLoopbackAddress();
    }

    @Override
    public int getLocalPort() {
        return connected ? 0 : -1;
    }

    @Override
    public SocketAddress getRemoteSocketAddress() {
        return connected && peerAddress != null
                ? new InetSocketAddress(peerAddress, port) : null;
    }

    @Override
    public boolean isConnected() {
        return connected;
    }

    @Override
    public boolean isBound() {
        return connected;
    }

    @Override
    public boolean isClosed() {
        return closed;
    }

    @Override
    public boolean isInputShutdown() {
        return inputShutdown;
    }

    @Override
    public boolean isOutputShutdown() {
        return outputShutdown;
    }

    @Override
    public synchronized void setSoTimeout(int timeout) throws SocketException {
        if (timeout < 0) throw new IllegalArgumentException("timeout can't be negative");
        soTimeoutMs = timeout;
    }

    @Override
    public synchronized int getSoTimeout() {
        return soTimeoutMs;
    }

    @Override
    public void shutdownInput() throws IOException {
        ensureOpen();
        inputShutdown = true;
    }

    @Override
    public void shutdownOutput() throws IOException {
        ensureOpen();
        outputShutdown = true;
    }

    // 宿主 TLS socket 选项不可透传：保持 Java 面可调用（OkHttp/Okio 会 set/get）。
    @Override
    public void setTcpNoDelay(boolean on) throws SocketException {
    }

    @Override
    public boolean getTcpNoDelay() {
        return true;
    }

    @Override
    public void setKeepAlive(boolean on) throws SocketException {
    }

    @Override
    public boolean getKeepAlive() {
        return true;
    }

    @Override
    public void setReuseAddress(boolean on) throws SocketException {
    }

    @Override
    public boolean getReuseAddress() {
        return false;
    }

    @Override
    public void setSoLinger(boolean on, int linger) throws SocketException {
    }

    @Override
    public int getSoLinger() {
        return -1;
    }

    @Override
    public void setSendBufferSize(int size) throws SocketException {
    }

    @Override
    public int getSendBufferSize() {
        return 0;
    }

    @Override
    public void setReceiveBufferSize(int size) throws SocketException {
    }

    @Override
    public int getReceiveBufferSize() {
        return 0;
    }

    @Override
    public void setTrafficClass(int tc) throws SocketException {
    }

    @Override
    public int getTrafficClass() {
        return 0;
    }

    @Override
    public String toString() {
        return "BridgedSSLSocket[" + (connected ? host + ":" + port : "unconnected")
                + (closed ? ",closed" : "") + "]";
    }

    // ------------------------------------------------------------------
    // SSLSocket 面
    // ------------------------------------------------------------------

    @Override
    public String[] getSupportedCipherSuites() {
        return DEFAULT_CIPHER_SUITES.clone();
    }

    @Override
    public String[] getEnabledCipherSuites() {
        return enabledCipherSuites.clone();
    }

    @Override
    public void setEnabledCipherSuites(String[] suites) {
        if (suites == null) throw new IllegalArgumentException("suites == null");
        enabledCipherSuites = suites.clone();
    }

    @Override
    public String[] getSupportedProtocols() {
        return DEFAULT_PROTOCOLS.clone();
    }

    @Override
    public String[] getEnabledProtocols() {
        return enabledProtocols.clone();
    }

    @Override
    public void setEnabledProtocols(String[] protocols) {
        if (protocols == null) throw new IllegalArgumentException("protocols == null");
        enabledProtocols = protocols.clone();
    }

    @Override
    public SSLSession getSession() {
        if (session == null) {
            session = new BridgedSSLSession("TLSv1.2", "TLS_AES_128_GCM_SHA256", host, port,
                    new Certificate[0]);
        }
        return session;
    }

    @Override
    public SSLSession getHandshakeSession() {
        return getSession();
    }

    @Override
    public void addHandshakeCompletedListener(HandshakeCompletedListener listener) {
        if (listener == null) throw new IllegalArgumentException("listener == null");
        handshakeListeners.add(listener);
    }

    @Override
    public void removeHandshakeCompletedListener(HandshakeCompletedListener listener) {
        handshakeListeners.remove(listener);
    }

    @Override
    public void startHandshake() throws IOException {
        ensureOpen();  // 宿主 OPEN 已完成握手（证书/主机名校验在宿主栈）
    }

    @Override
    public void setUseClientMode(boolean mode) {
        useClientMode = mode;
    }

    @Override
    public boolean getUseClientMode() {
        return useClientMode;
    }

    @Override
    public void setNeedClientAuth(boolean need) {
        needClientAuth = need;
        if (need) wantClientAuth = false;
    }

    @Override
    public boolean getNeedClientAuth() {
        return needClientAuth;
    }

    @Override
    public void setWantClientAuth(boolean want) {
        wantClientAuth = want;
        if (want) needClientAuth = false;
    }

    @Override
    public boolean getWantClientAuth() {
        return wantClientAuth;
    }

    @Override
    public void setEnableSessionCreation(boolean flag) {
        enableSessionCreation = flag;
    }

    @Override
    public boolean getEnableSessionCreation() {
        return enableSessionCreation;
    }

    @Override
    public String getApplicationProtocol() {
        return null;  // 宿主未启用 ALPN（HTTP/1.1 语义）
    }

    @Override
    public String getHandshakeApplicationProtocol() {
        return null;
    }

    static String[] defaultCipherSuites() {
        return DEFAULT_CIPHER_SUITES.clone();
    }

    static String[] defaultProtocols() {
        return DEFAULT_PROTOCOLS.clone();
    }
}
