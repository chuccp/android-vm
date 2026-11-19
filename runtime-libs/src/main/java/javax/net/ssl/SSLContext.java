package javax.net.ssl;

import java.security.*;
import java.util.Locale;
import java.util.Objects;

import sun.security.jca.GetInstance;

public class SSLContext {
    private final Provider provider;

    private final SSLContextSpi contextSpi;

    private final String protocol;

    protected SSLContext(SSLContextSpi contextSpi, Provider provider,
            String protocol) {
        this.contextSpi = contextSpi;
        this.provider = provider;
        this.protocol = protocol;
    }

    private static SSLContext defaultContext;

    public static synchronized SSLContext getDefault()
            throws NoSuchAlgorithmException {
        if (defaultContext == null) {
            defaultContext = SSLContext.getInstance("Default");
        }
        return defaultContext;
    }

    public static synchronized void setDefault(SSLContext context) {
        if (context == null) {
            throw new NullPointerException();
        }
        SecurityManager sm = System.getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new SSLPermission("setDefaultSSLContext"));
        }
        defaultContext = context;
    }

    // TLS 桥接口径（bridge-first）：lib 无 SSL provider（无 sun.security.ssl /
    // Conscrypt），TLS 语义由宿主真 TLS 栈承接。getInstance("TLS"/"TLSv1.x"/
    // "Default") 在 provider 查找无实现时返回桥接上下文，而不是把 null SPI
    // 包进 SSLContext（旧路径 getSocketFactory 直接 NPE）。
    private static boolean isTlsProtocol(String protocol) {
        if (protocol == null) return false;
        String p = protocol.toUpperCase(Locale.ENGLISH);
        return p.equals("DEFAULT") || p.equals("SSL") || p.startsWith("TLS")
                || p.startsWith("SSL");
    }

    private static SSLContext bridged(String protocol) {
        return new SSLContext(new BridgedSSLContextSpi(), null, protocol);
    }

    public static SSLContext getInstance(String protocol)
            throws NoSuchAlgorithmException {
        Objects.requireNonNull(protocol, "null protocol name");
        try {
            GetInstance.Instance instance = GetInstance.getInstance
                    ("SSLContext", SSLContextSpi.class, protocol);
            if (instance.impl != null) {
                return new SSLContext((SSLContextSpi)instance.impl,
                        instance.provider, protocol);
            }
        } catch (NoSuchAlgorithmException ignored) {
            // lib 无 SSL provider：服务表未命中走宿主桥接（GetInstance 实装后
            // 未命中抛 NoSuchAlgorithmException，而非旧桩的 null-impl 返回）。
        }
        if (isTlsProtocol(protocol)) {
            return bridged(protocol);
        }
        throw new NoSuchAlgorithmException("SSLContext " + protocol);
    }

    public static SSLContext getInstance(String protocol, String provider)
            throws NoSuchAlgorithmException, NoSuchProviderException {
        Objects.requireNonNull(protocol, "null protocol name");
        try {
            GetInstance.Instance instance = GetInstance.getInstance
                    ("SSLContext", SSLContextSpi.class, protocol, provider);
            if (instance.impl != null) {
                return new SSLContext((SSLContextSpi)instance.impl,
                        instance.provider, protocol);
            }
        } catch (NoSuchAlgorithmException ignored) {
        }
        if (isTlsProtocol(protocol)) {
            return bridged(protocol);
        }
        throw new NoSuchAlgorithmException("SSLContext " + protocol);
    }

    public static SSLContext getInstance(String protocol, Provider provider)
            throws NoSuchAlgorithmException {
        Objects.requireNonNull(protocol, "null protocol name");
        try {
            GetInstance.Instance instance = GetInstance.getInstance
                    ("SSLContext", SSLContextSpi.class, protocol, provider);
            if (instance.impl != null) {
                return new SSLContext((SSLContextSpi)instance.impl,
                        instance.provider, protocol);
            }
        } catch (NoSuchAlgorithmException ignored) {
        }
        if (isTlsProtocol(protocol)) {
            return bridged(protocol);
        }
        throw new NoSuchAlgorithmException("SSLContext " + protocol);
    }

    public final String getProtocol() {
        return this.protocol;
    }

    public final Provider getProvider() {
        return this.provider;
    }

    public final void init(KeyManager[] km, TrustManager[] tm,
                                SecureRandom random)
        throws KeyManagementException {
        contextSpi.engineInit(km, tm, random);
    }

    public final SSLSocketFactory getSocketFactory() {
        return contextSpi.engineGetSocketFactory();
    }

    public final SSLServerSocketFactory getServerSocketFactory() {
        return contextSpi.engineGetServerSocketFactory();
    }

    public final SSLEngine createSSLEngine() {
        try {
            return contextSpi.engineCreateSSLEngine();
        } catch (AbstractMethodError e) {
            UnsupportedOperationException unsup =
                new UnsupportedOperationException(
                    "Provider: " + getProvider() +
                    " doesn't support this operation");
            unsup.initCause(e);
            throw unsup;
        }
    }

    public final SSLEngine createSSLEngine(String peerHost, int peerPort) {
        try {
            return contextSpi.engineCreateSSLEngine(peerHost, peerPort);
        } catch (AbstractMethodError e) {
            UnsupportedOperationException unsup =
                new UnsupportedOperationException(
                    "Provider: " + getProvider() +
                    " does not support this operation");
            unsup.initCause(e);
            throw unsup;
        }
    }

    public final SSLSessionContext getServerSessionContext() {
        return contextSpi.engineGetServerSessionContext();
    }

    public final SSLSessionContext getClientSessionContext() {
        return contextSpi.engineGetClientSessionContext();
    }

    public final SSLParameters getDefaultSSLParameters() {
        return contextSpi.engineGetDefaultSSLParameters();
    }

    public final SSLParameters getSupportedSSLParameters() {
        return contextSpi.engineGetSupportedSSLParameters();
    }

}
