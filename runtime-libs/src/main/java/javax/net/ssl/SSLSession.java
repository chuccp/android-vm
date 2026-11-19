package javax.net.ssl;

import java.security.Principal;

public interface SSLSession {

    public byte[] getId();

    public SSLSessionContext getSessionContext();

    public long getCreationTime();

    public long getLastAccessedTime();

    public void invalidate();

    public boolean isValid();

    public void putValue(String name, Object value);

    public Object getValue(String name);

    public void removeValue(String name);

    public String [] getValueNames();

    public java.security.cert.Certificate [] getPeerCertificates()
            throws SSLPeerUnverifiedException;

    public java.security.cert.Certificate [] getLocalCertificates();

    @Deprecated(since="9")
    public javax.security.cert.X509Certificate [] getPeerCertificateChain()
            throws SSLPeerUnverifiedException;

    public Principal getPeerPrincipal()
            throws SSLPeerUnverifiedException;

    public Principal getLocalPrincipal();

    public String getCipherSuite();

    public String getProtocol();

    public String getPeerHost();

    public int getPeerPort();

    public int getPacketBufferSize();

    public int getApplicationBufferSize();
}
