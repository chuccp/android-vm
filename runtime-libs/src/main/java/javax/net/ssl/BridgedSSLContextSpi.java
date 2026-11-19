package javax.net.ssl;

import java.security.KeyManagementException;
import java.security.SecureRandom;

/**
 * 宿主 TLS 桥接 SSLContext SPI。
 *
 * init(km/tm/random) 为空转：guest 配置的 TrustManager/KeyManager 不参与真实
 * 校验（证书链/主机名由宿主 TLS 栈按系统信任锚校验，不跳过校验）；客户端证书
 * 面（KeyManager）暂未桥接（见 tls_bridge.h 已知缺口）。
 */
final class BridgedSSLContextSpi extends SSLContextSpi {

    @Override
    protected void engineInit(KeyManager[] km, TrustManager[] tm, SecureRandom sr)
            throws KeyManagementException {
    }

    @Override
    protected SSLSocketFactory engineGetSocketFactory() {
        return new BridgedSSLSocketFactory();
    }

    @Override
    protected SSLServerSocketFactory engineGetServerSocketFactory() {
        throw new UnsupportedOperationException("TLS bridge is client-side only");
    }

    @Override
    protected SSLEngine engineCreateSSLEngine() {
        throw new UnsupportedOperationException("SSLEngine is not bridged");
    }

    @Override
    protected SSLEngine engineCreateSSLEngine(String host, int port) {
        throw new UnsupportedOperationException("SSLEngine is not bridged");
    }

    @Override
    protected SSLSessionContext engineGetServerSessionContext() {
        return null;
    }

    @Override
    protected SSLSessionContext engineGetClientSessionContext() {
        return null;
    }
}
