package javax.net.ssl;

import java.security.*;

public abstract class TrustManagerFactorySpi {

    protected abstract void engineInit(KeyStore ks) throws KeyStoreException;

    protected abstract void engineInit(ManagerFactoryParameters spec)
        throws InvalidAlgorithmParameterException;

    protected abstract TrustManager[] engineGetTrustManagers();
}
