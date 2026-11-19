package javax.net.ssl;

import java.security.Security;
import java.security.*;
import java.util.Objects;

import sun.security.jca.GetInstance;

public class TrustManagerFactory {

    private Provider provider;

    private TrustManagerFactorySpi factorySpi;

    private String algorithm;

    public static final String getDefaultAlgorithm() {
        String type;
        type = AccessController.doPrivileged(new PrivilegedAction<>() {
            @Override
            public String run() {
                return Security.getProperty(
                    "ssl.TrustManagerFactory.algorithm");
            }
        });
        if (type == null) {
            type = "SunX509";
        }
        return type;
    }

    protected TrustManagerFactory(TrustManagerFactorySpi factorySpi,
            Provider provider, String algorithm) {
        this.factorySpi = factorySpi;
        this.provider = provider;
        this.algorithm = algorithm;
    }

    public final String getAlgorithm() {
        return this.algorithm;
    }

    public static final TrustManagerFactory getInstance(String algorithm)
            throws NoSuchAlgorithmException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        try {
            GetInstance.Instance instance = GetInstance.getInstance
                    ("TrustManagerFactory", TrustManagerFactorySpi.class,
                    algorithm);
            if (instance.impl != null) {
                return new TrustManagerFactory(
                        (TrustManagerFactorySpi)instance.impl,
                        instance.provider, algorithm);
            }
        } catch (NoSuchAlgorithmException ignored) {
            // lib 无 JSSE provider（SunX509/PKIX 均无实现）：信任判定桥接宿主
            // TLS 栈（OkHttp Util.platformTrustManager 的入口；GetInstance
            // 实装后未命中抛异常而非旧桩的 null-impl 返回）。
        }
        return new TrustManagerFactory(new BridgedTrustManagerFactorySpi(),
                null, algorithm);
    }

    public static final TrustManagerFactory getInstance(String algorithm,
            String provider) throws NoSuchAlgorithmException,
            NoSuchProviderException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        try {
            GetInstance.Instance instance = GetInstance.getInstance
                    ("TrustManagerFactory", TrustManagerFactorySpi.class,
                    algorithm, provider);
            if (instance.impl != null) {
                return new TrustManagerFactory(
                        (TrustManagerFactorySpi)instance.impl,
                        instance.provider, algorithm);
            }
        } catch (NoSuchAlgorithmException ignored) {
        }
        return new TrustManagerFactory(new BridgedTrustManagerFactorySpi(),
                null, algorithm);
    }

    public static final TrustManagerFactory getInstance(String algorithm,
            Provider provider) throws NoSuchAlgorithmException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        try {
            GetInstance.Instance instance = GetInstance.getInstance
                    ("TrustManagerFactory", TrustManagerFactorySpi.class,
                    algorithm, provider);
            if (instance.impl != null) {
                return new TrustManagerFactory(
                        (TrustManagerFactorySpi)instance.impl,
                        instance.provider, algorithm);
            }
        } catch (NoSuchAlgorithmException ignored) {
        }
        return new TrustManagerFactory(new BridgedTrustManagerFactorySpi(),
                null, algorithm);
    }

    public final Provider getProvider() {
        return this.provider;
    }

    public final void init(KeyStore ks) throws KeyStoreException {
        factorySpi.engineInit(ks);
    }

    public final void init(ManagerFactoryParameters spec) throws
            InvalidAlgorithmParameterException {
        factorySpi.engineInit(spec);
    }

    public final TrustManager[] getTrustManagers() {
        return factorySpi.engineGetTrustManagers();
    }
}
