package javax.net.ssl;

import java.security.InvalidAlgorithmParameterException;
import java.security.KeyStore;
import java.security.KeyStoreException;

/** lib 无 JSSE provider：TrustManagerFactory 的宿主桥接 SPI（信任判定在宿主）。 */
final class BridgedTrustManagerFactorySpi extends TrustManagerFactorySpi {
    private static final TrustManager[] MANAGERS = {new BridgedX509TrustManager()};

    @Override
    protected void engineInit(KeyStore ks) throws KeyStoreException {
    }

    @Override
    protected void engineInit(ManagerFactoryParameters spec)
            throws InvalidAlgorithmParameterException {
    }

    @Override
    protected TrustManager[] engineGetTrustManagers() {
        return MANAGERS.clone();
    }
}
