package javax.net.ssl;

import java.net.Socket;
import javax.net.ssl.X509TrustManager;

import java.security.cert.X509Certificate;
import java.security.cert.CertificateException;

public abstract class X509ExtendedTrustManager implements X509TrustManager {

    public abstract void checkClientTrusted(X509Certificate[] chain,
            String authType, Socket socket) throws CertificateException;

    public abstract void checkServerTrusted(X509Certificate[] chain,
        String authType, Socket socket) throws CertificateException;

    public abstract void checkClientTrusted(X509Certificate[] chain,
        String authType, SSLEngine engine) throws CertificateException;

    public abstract void checkServerTrusted(X509Certificate[] chain,
        String authType, SSLEngine engine) throws CertificateException;

}
