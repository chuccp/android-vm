package javax.net.ssl;

import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;

/**
 * 宿主 TLS 信任面的 guest 投影。
 *
 * 真实证书链/主机名校验在宿主 TLS 栈（Android 系统信任锚 + HTTPS endpoint
 * identification）完成，本类 check* 为空转、acceptedIssuers 为空——OkHttp 只在
 * 无 pin 时用它构造 CertificateChainCleaner，不参与握手校验。
 */
final class BridgedX509TrustManager implements X509TrustManager {
    private static final X509Certificate[] EMPTY = new X509Certificate[0];

    @Override
    public void checkClientTrusted(X509Certificate[] chain, String authType)
            throws CertificateException {
        // 宿主已校验（客户端证书面未桥接；服务端信任不在此重复判定）。
    }

    @Override
    public void checkServerTrusted(X509Certificate[] chain, String authType)
            throws CertificateException {
        // 宿主已校验。
    }

    @Override
    public X509Certificate[] getAcceptedIssuers() {
        return EMPTY.clone();
    }
}
