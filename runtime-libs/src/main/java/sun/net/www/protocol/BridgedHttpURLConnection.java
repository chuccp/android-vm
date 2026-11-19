package sun.net.www.protocol;

import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URL;
import java.security.Principal;
import java.security.cert.Certificate;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLPeerUnverifiedException;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * VM 侧 HttpURLConnection/HttpsURLConnection 实现（lib 优先面）：
 * runtime.dex 未随包 sun.net.www.protocol.{http,https}.Handler（JDK 的
 * URL 协议实现依赖 HttpClient/KeepAliveCache 全套），URL.openConnection()
 * 因此抛 "unknown protocol: https"，zcloud/FileDownloader 等 URLConnection
 * 下载链全部失效（RN bundle 同步超时的根因）。
 *
 * 本类用 guest 自己的 Socket / BridgedSSLSocket（宿主 TLS 桥）实现最小
 * HTTP/1.1 客户端面：请求头透传、状态行/响应头解析、chunked 与
 * Content-Length 响应体、3xx 跟随（限 5 跳）、超时映射。
 */
public class BridgedHttpURLConnection extends HttpsURLConnection {

    private final String scheme;
    private final Map<String, List<String>> requestHeaders = new LinkedHashMap<>();
    private String requestMethod;
    private boolean followRedirects = true;

    private Socket socket;
    private InputStream socketIn;
    private OutputStream socketOut;
    private boolean responseParsed;
    private int responseCode = -1;
    private String responseMessage = "";
    private final Map<String, List<String>> responseHeaders = new LinkedHashMap<>();
    private InputStream bodyStream;

    public BridgedHttpURLConnection(URL url, String scheme) {
        super(url);
        this.scheme = scheme;
    }

    // ---- 请求面 ----

    @Override
    public void setRequestMethod(String method) throws java.net.ProtocolException {
        if (method == null) throw new NullPointerException();
        String upper = method.toUpperCase(Locale.ROOT);
        if (!"GET".equals(upper) && !"POST".equals(upper) && !"HEAD".equals(upper)
                && !"PUT".equals(upper) && !"DELETE".equals(upper)) {
            throw new java.net.ProtocolException("Invalid HTTP method: " + method);
        }
        this.requestMethod = upper;
    }

    @Override
    public String getRequestMethod() {
        return requestMethod != null ? requestMethod : (doOutput ? "POST" : "GET");
    }

    @Override
    public void setInstanceFollowRedirects(boolean followRedirects) {
        this.followRedirects = followRedirects;
    }

    @Override
    public boolean getInstanceFollowRedirects() {
        return followRedirects;
    }

    @Override
    public boolean usingProxy() {
        return false;
    }

    @Override
    public void setRequestProperty(String key, String value) {
        if (connected) throw new IllegalStateException("Already connected");
        requestHeaders.remove(key);
        addRequestProperty(key, value);
    }

    @Override
    public void addRequestProperty(String key, String value) {
        if (connected) throw new IllegalStateException("Already connected");
        List<String> values = requestHeaders.get(key);
        if (values == null) {
            values = new ArrayList<>();
            requestHeaders.put(key, values);
        }
        values.add(value);
    }

    @Override
    public String getRequestProperty(String key) {
        List<String> values = requestHeaders.get(key);
        return values == null || values.isEmpty() ? null : values.get(0);
    }

    // ---- 连接与响应 ----

    @Override
    public void connect() throws IOException {
        if (connected) return;
        doConnect(url, 0);
        connected = true;
    }

    private void doConnect(URL target, int redirects) throws IOException {
        String host = target.getHost();
        int port = target.getPort() != -1 ? target.getPort() : target.getDefaultPort();
        int connectTimeout = getConnectTimeout();
        int readTimeout = getReadTimeout();
        Socket s;
        if ("https".equals(target.getProtocol())) {
            SSLSocketFactory factory = (SSLSocketFactory) SSLSocketFactory.getDefault();
            s = factory.createSocket(host, port);
        } else {
            s = new Socket();
            s.connect(new InetSocketAddress(host, port), connectTimeout > 0 ? connectTimeout : 15000);
        }
        s.setSoTimeout(readTimeout > 0 ? readTimeout : 30000);
        if (s instanceof SSLSocket) {
            try {
                ((SSLSocket) s).startHandshake();
            } catch (IOException e) {
                closeQuietly(s);
                throw e;
            }
        }
        socket = s;
        socketIn = s.getInputStream();
        socketOut = s.getOutputStream();

        String method = getRequestMethod();
        String file = target.getFile();
        if (file == null || file.isEmpty()) file = "/";
        StringBuilder head = new StringBuilder();
        head.append(method).append(' ').append(file).append(" HTTP/1.1\r\n");
        head.append("Host: ").append(host);
        if (target.getPort() != -1) head.append(':').append(target.getPort());
        head.append("\r\n");
        head.append("Connection: close\r\n");
        head.append("Accept-Encoding: identity\r\n");
        head.append("User-Agent: dexvm/1.0\r\n");
        for (Map.Entry<String, List<String>> entry : requestHeaders.entrySet()) {
            if ("host".equalsIgnoreCase(entry.getKey())
                    || "connection".equalsIgnoreCase(entry.getKey())) {
                continue;  // 由实现掌控的连接头
            }
            for (String value : entry.getValue()) {
                head.append(entry.getKey()).append(": ").append(value).append("\r\n");
            }
        }
        head.append("\r\n");
        socketOut.write(head.toString().getBytes("ISO-8859-1"));
        socketOut.flush();

        parseResponseHead();

        if (followRedirects && isRedirect(responseCode) && redirects < 5) {
            String location = header("Location");
            if (location != null && !location.isEmpty()) {
                URL next = new URL(target, location);
                disconnectSocket();
                responseCode = -1;
                responseMessage = "";
                responseHeaders.clear();
                bodyStream = null;
                responseParsed = false;
                doConnect(next, redirects + 1);
            }
        }
    }

    private static boolean isRedirect(int code) {
        return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
    }

    private void parseResponseHead() throws IOException {
        String statusLine = readLine(socketIn);
        if (statusLine == null) throw new EOFException("unexpected end of stream");
        // "HTTP/1.1 200 OK"
        int sp1 = statusLine.indexOf(' ');
        if (sp1 < 0 || !statusLine.startsWith("HTTP/")) {
            throw new IOException("invalid HTTP response: " + statusLine);
        }
        int sp2 = statusLine.indexOf(' ', sp1 + 1);
        String codeText = sp2 < 0 ? statusLine.substring(sp1 + 1)
                : statusLine.substring(sp1 + 1, sp2);
        try {
            responseCode = Integer.parseInt(codeText.trim());
        } catch (NumberFormatException e) {
            throw new IOException("invalid status code: " + statusLine);
        }
        responseMessage = sp2 < 0 ? "" : statusLine.substring(sp2 + 1);
        // 响应头
        while (true) {
            String line = readLine(socketIn);
            if (line == null) throw new EOFException("unexpected end of headers");
            if (line.isEmpty()) break;
            int colon = line.indexOf(':');
            if (colon <= 0) continue;
            String key = line.substring(0, colon).trim();
            String value = line.substring(colon + 1).trim();
            String lower = key.toLowerCase(Locale.ROOT);
            List<String> list = responseHeaders.get(lower);
            if (list == null) {
                list = new ArrayList<>();
                responseHeaders.put(lower, list);
            }
            list.add(value);
            headerKeyOrder.add(key);
            headerValueOrder.add(value);
        }
        responseParsed = true;
        bodyStream = createBodyStream();
    }

    private final List<String> headerKeyOrder = new ArrayList<>();
    private final List<String> headerValueOrder = new ArrayList<>();

    private InputStream createBodyStream() throws IOException {
        if ("HEAD".equals(getRequestMethod()) || responseCode == 204 || responseCode == 304) {
            return new java.io.ByteArrayInputStream(new byte[0]);
        }
        // 响应体立即完整读入内存：FileDownloader/zcloud 的落盘/MD5 校验依赖
        // 字节完整性，缓冲化避免流式读取路径的边界瑕疵（文件量级 MB 内）。
        java.io.ByteArrayOutputStream buffer = new java.io.ByteArrayOutputStream(16 * 1024);
        String transferEncoding = header("Transfer-Encoding");
        if (transferEncoding != null
                && transferEncoding.toLowerCase(Locale.ROOT).contains("chunked")) {
            ChunkedInputStream chunked = new ChunkedInputStream(socketIn);
            byte[] buf = new byte[8192];
            int n;
            while ((n = chunked.read(buf, 0, buf.length)) >= 0) {
                if (n > 0) buffer.write(buf, 0, n);
            }
            return new java.io.ByteArrayInputStream(buffer.toByteArray());
        }
        long want = -1;
        String contentLength = header("Content-Length");
        if (contentLength != null) {
            try {
                want = Long.parseLong(contentLength.trim());
            } catch (NumberFormatException ignored) {
            }
        }
        byte[] buf = new byte[8192];
        long got = 0;
        while (true) {
            int ask = buf.length;
            if (want >= 0) {
                long left = want - got;
                if (left <= 0) break;
                if (left < ask) ask = (int) left;
            }
            int n = socketIn.read(buf, 0, ask);
            if (n < 0) break;
            if (n == 0) continue;
            buffer.write(buf, 0, n);
            got += n;
        }
        return new java.io.ByteArrayInputStream(buffer.toByteArray());
    }

    private String header(String name) {
        List<String> values = responseHeaders.get(name.toLowerCase(Locale.ROOT));
        return values == null || values.isEmpty() ? null : values.get(0);
    }

    private static String readLine(InputStream in) throws IOException {
        StringBuilder sb = new StringBuilder(64);
        int c;
        while ((c = in.read()) != -1) {
            if (c == '\n') {
                int len = sb.length();
                if (len > 0 && sb.charAt(len - 1) == '\r') sb.setLength(len - 1);
                return sb.toString();
            }
            sb.append((char) c);
        }
        return sb.length() == 0 ? null : sb.toString();
    }

    private void ensureResponse() throws IOException {
        connect();
        if (!responseParsed) throw new IOException("no response");
    }

    @Override
    public int getResponseCode() throws IOException {
        ensureResponse();
        return responseCode;
    }

    @Override
    public String getResponseMessage() throws IOException {
        ensureResponse();
        return responseMessage;
    }

    @Override
    public String getHeaderFieldKey(int n) {
        if (n < 0 || n >= headerKeyOrder.size()) return null;
        return headerKeyOrder.get(n);
    }

    @Override
    public String getHeaderField(int n) {
        if (n < 0 || n >= headerValueOrder.size()) return null;
        return headerValueOrder.get(n);
    }

    @Override
    public String getHeaderField(String name) {
        if (name == null) return null;
        return header(name);
    }

    @Override
    public Map<String, List<String>> getHeaderFields() {
        return Collections.unmodifiableMap(responseHeaders);
    }

    @Override
    public long getHeaderFieldLong(String name, long defaultValue) {
        String value = header(name);
        if (value == null) return defaultValue;
        try {
            return Long.parseLong(value.trim());
        } catch (NumberFormatException e) {
            return defaultValue;
        }
    }

    @Override
    public InputStream getInputStream() throws IOException {
        ensureResponse();
        if (responseCode >= 400) {
            throw new IOException("Server returned HTTP " + responseCode);
        }
        return bodyStream;
    }

    @Override
    public InputStream getErrorStream() {
        try {
            ensureResponse();
        } catch (IOException e) {
            return null;
        }
        return responseCode >= 400 ? bodyStream : null;
    }

    @Override
    public OutputStream getOutputStream() throws IOException {
        if (!connected) connect();
        return socketOut;
    }

    @Override
    public void disconnect() {
        disconnectSocket();
    }

    private void disconnectSocket() {
        closeQuietly(socket);
        socket = null;
        socketIn = null;
        socketOut = null;
    }

    private static void closeQuietly(Socket s) {
        if (s == null) return;
        try {
            s.close();
        } catch (IOException ignored) {
        }
    }

    // ---- TLS 身份面（宿主 TLS 桥不暴露证书；下载链不消费） ----

    @Override
    public String getCipherSuite() {
        return "TLS";
    }

    @Override
    public Certificate[] getLocalCertificates() {
        return null;
    }

    @Override
    public Certificate[] getServerCertificates() throws SSLPeerUnverifiedException {
        return null;
    }

    public Principal getPeerPrincipal() throws SSLPeerUnverifiedException {
        return null;
    }

    // ---- 响应体流 ----

    private static final class FixedLengthInputStream extends InputStream {
        private final InputStream in;
        private long remaining;

        FixedLengthInputStream(InputStream in, long length) {
            this.in = in;
            this.remaining = length;
        }

        @Override
        public int read() throws IOException {
            if (remaining <= 0) return -1;
            int c = in.read();
            if (c >= 0) remaining--;
            return c;
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (remaining <= 0) return -1;
            int want = (int) Math.min(len, remaining);
            int n = in.read(b, off, want);
            if (n > 0) remaining -= n;
            return n;
        }

        @Override
        public int available() {
            return (int) Math.min(Integer.MAX_VALUE, Math.max(0, remaining));
        }
    }

    private static final class ChunkedInputStream extends InputStream {
        private final InputStream in;
        private long chunkRemaining;
        private boolean eof;

        ChunkedInputStream(InputStream in) {
            this.in = in;
        }

        private boolean nextChunk() throws IOException {
            if (eof) return false;
            if (chunkRemaining > 0) return true;
            String line = readLine(in);
            if (line == null) {
                eof = true;
                return false;
            }
            int semicolon = line.indexOf(';');
            if (semicolon >= 0) line = line.substring(0, semicolon);
            line = line.trim();
            if (line.isEmpty()) {
                // 上一 chunk 结束的 CRLF
                line = readLine(in);
                if (line == null) {
                    eof = true;
                    return false;
                }
            }
            long size;
            try {
                size = Long.parseLong(line, 16);
            } catch (NumberFormatException e) {
                throw new IOException("invalid chunk size: " + line);
            }
            if (size == 0) {
                // trailer headers
                while (true) {
                    String trailer = readLine(in);
                    if (trailer == null || trailer.isEmpty()) break;
                }
                eof = true;
                return false;
            }
            chunkRemaining = size;
            return true;
        }

        @Override
        public int read() throws IOException {
            byte[] one = new byte[1];
            int n = read(one, 0, 1);
            return n < 0 ? -1 : one[0] & 0xFF;
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (!nextChunk()) return -1;
            int want = (int) Math.min(len, chunkRemaining);
            int n = in.read(b, off, want);
            if (n > 0) {
                chunkRemaining -= n;
                if (chunkRemaining == 0) {
                    // 丢弃 chunk 尾部 CRLF
                    readLine(in);
                }
            }
            return n;
        }
    }
}
