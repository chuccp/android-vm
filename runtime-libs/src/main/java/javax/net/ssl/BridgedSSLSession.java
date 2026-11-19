package javax.net.ssl;

import java.security.Principal;
import java.security.cert.Certificate;
import java.util.HashMap;
import java.util.Map;

/**
 * 宿主 TLS 会话的 guest 视图：协议/cipher/对端证书身份来自宿主握手结果
 * （core/tls_bridge 的 OPEN 元数据），会话缓存/有效期面为无状态投影。
 */
final class BridgedSSLSession implements SSLSession {
    private static final byte[] EMPTY_ID = new byte[0];

    private final String protocol;
    private final String cipherSuite;
    private final String peerHost;
    private final int peerPort;
    private final Certificate[] peerCertificates;
    private final Map<String, Object> values = new HashMap<>();
    private final long creationTime = System.currentTimeMillis();
    private volatile boolean valid = true;

    BridgedSSLSession(String protocol, String cipherSuite, String peerHost, int peerPort,
                      Certificate[] peerCertificates) {
        this.protocol = protocol == null || protocol.isEmpty() ? "TLSv1.2" : protocol;
        this.cipherSuite = cipherSuite == null || cipherSuite.isEmpty()
                ? "TLS_AES_128_GCM_SHA256" : cipherSuite;
        this.peerHost = peerHost;
        this.peerPort = peerPort;
        this.peerCertificates = peerCertificates;
    }

    @Override
    public byte[] getId() {
        return EMPTY_ID.clone();
    }

    @Override
    public SSLSessionContext getSessionContext() {
        return null;
    }

    @Override
    public long getCreationTime() {
        return creationTime;
    }

    @Override
    public long getLastAccessedTime() {
        return creationTime;
    }

    @Override
    public void invalidate() {
        valid = false;
    }

    @Override
    public boolean isValid() {
        return valid;
    }

    @Override
    public void putValue(String name, Object value) {
        if (name != null) values.put(name, value);
    }

    @Override
    public Object getValue(String name) {
        return name == null ? null : values.get(name);
    }

    @Override
    public void removeValue(String name) {
        if (name != null) values.remove(name);
    }

    @Override
    public String[] getValueNames() {
        return values.keySet().toArray(new String[0]);
    }

    @Override
    public Certificate[] getPeerCertificates() throws SSLPeerUnverifiedException {
        if (peerCertificates == null || peerCertificates.length == 0) {
            throw new SSLPeerUnverifiedException("peer certificate unavailable");
        }
        return peerCertificates.clone();
    }

    @Override
    public Certificate[] getLocalCertificates() {
        return null;
    }

    @Override
    public javax.security.cert.X509Certificate[] getPeerCertificateChain()
            throws SSLPeerUnverifiedException {
        throw new SSLPeerUnverifiedException("peer certificate chain unavailable");
    }

    @Override
    public Principal getPeerPrincipal() throws SSLPeerUnverifiedException {
        return null;
    }

    @Override
    public Principal getLocalPrincipal() {
        return null;
    }

    @Override
    public String getCipherSuite() {
        return cipherSuite;
    }

    @Override
    public String getProtocol() {
        return protocol;
    }

    @Override
    public String getPeerHost() {
        return peerHost;
    }

    @Override
    public int getPeerPort() {
        return peerPort;
    }

    @Override
    public int getPacketBufferSize() {
        return 16709;
    }

    @Override
    public int getApplicationBufferSize() {
        return 16384;
    }
}
