package sun.net.www.protocol.https;

import java.io.IOException;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandler;

import sun.net.www.protocol.BridgedHttpURLConnection;

/**
 * https 协议 handler（URL.openConnection 的 sun.net.www.protocol 面）。
 * JDK 原实现依赖 HttpsClient/KeepAliveCache 全套；VM 以 guest Socket +
 * BridgedSSLSocket（宿主 TLS 桥）承接，见 BridgedHttpURLConnection。
 */
public class Handler extends URLStreamHandler {

    public Handler() {
    }

    @Override
    protected URLConnection openConnection(URL u) throws IOException {
        return new BridgedHttpURLConnection(u, "https");
    }

    @Override
    protected int getDefaultPort() {
        return 443;
    }
}
