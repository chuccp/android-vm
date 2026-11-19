package sun.net.www.protocol.http;

import java.io.IOException;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandler;

import sun.net.www.protocol.BridgedHttpURLConnection;

/**
 * http 协议 handler（同 https，见 BridgedHttpURLConnection；guest 明文 Socket 承接）。
 */
public class Handler extends URLStreamHandler {

    public Handler() {
    }

    @Override
    protected URLConnection openConnection(URL u) throws IOException {
        return new BridgedHttpURLConnection(u, "http");
    }

    @Override
    protected int getDefaultPort() {
        return 80;
    }
}
