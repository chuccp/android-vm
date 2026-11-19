package javax.net.ssl;

import java.security.Security;
import java.security.*;
import java.util.Objects;

import sun.security.jca.GetInstance;

public class KeyManagerFactory {

    private Provider provider;

    private KeyManagerFactorySpi factorySpi;

    private String algorithm;

    public static final String getDefaultAlgorithm() {
        String type;
        type = AccessController.doPrivileged(new PrivilegedAction<>() {
            @Override
            public String run() {
                return Security.getProperty(
                    "ssl.KeyManagerFactory.algorithm");
            }
        });
        if (type == null) {
            type = "SunX509";
        }
        return type;
    }

    protected KeyManagerFactory(KeyManagerFactorySpi factorySpi,
                                Provider provider, String algorithm) {
        this.factorySpi = factorySpi;
        this.provider = provider;
        this.algorithm = algorithm;
    }

    public final String getAlgorithm() {
        return this.algorithm;
    }

    public static final KeyManagerFactory getInstance(String algorithm)
            throws NoSuchAlgorithmException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        GetInstance.Instance instance = GetInstance.getInstance
                ("KeyManagerFactory", KeyManagerFactorySpi.class,
                algorithm);
        return new KeyManagerFactory((KeyManagerFactorySpi)instance.impl,
                instance.provider, algorithm);
    }

    public static final KeyManagerFactory getInstance(String algorithm,
            String provider) throws NoSuchAlgorithmException,
            NoSuchProviderException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        GetInstance.Instance instance = GetInstance.getInstance
                ("KeyManagerFactory", KeyManagerFactorySpi.class,
                algorithm, provider);
        return new KeyManagerFactory((KeyManagerFactorySpi)instance.impl,
                instance.provider, algorithm);
    }

    public static final KeyManagerFactory getInstance(String algorithm,
            Provider provider) throws NoSuchAlgorithmException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        GetInstance.Instance instance = GetInstance.getInstance
                ("KeyManagerFactory", KeyManagerFactorySpi.class,
                algorithm, provider);
        return new KeyManagerFactory((KeyManagerFactorySpi)instance.impl,
                instance.provider, algorithm);
    }

    public final Provider getProvider() {
        return this.provider;
    }

    public final void init(KeyStore ks, char[] password) throws
            KeyStoreException, NoSuchAlgorithmException,
            UnrecoverableKeyException {
        factorySpi.engineInit(ks, password);
    }

    public final void init(ManagerFactoryParameters spec) throws
            InvalidAlgorithmParameterException {
        factorySpi.engineInit(spec);
    }

    public final KeyManager[] getKeyManagers() {
        return factorySpi.engineGetKeyManagers();
    }
}
