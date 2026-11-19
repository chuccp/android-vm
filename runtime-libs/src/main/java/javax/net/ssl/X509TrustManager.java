package javax.net.ssl;

import java.security.cert.*;

public interface X509TrustManager extends TrustManager {

    public void checkClientTrusted(X509Certificate[] chain, String authType)
        throws CertificateException;

    public void checkServerTrusted(X509Certificate[] chain, String authType)
        throws CertificateException;

    public X509Certificate[] getAcceptedIssuers();
}
