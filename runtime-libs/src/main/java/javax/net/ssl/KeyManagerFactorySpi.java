package javax.net.ssl;

import java.security.*;

public abstract class KeyManagerFactorySpi {

    protected abstract void engineInit(KeyStore ks, char[] password) throws
        KeyStoreException, NoSuchAlgorithmException, UnrecoverableKeyException;

    protected abstract void engineInit(ManagerFactoryParameters spec)
        throws InvalidAlgorithmParameterException;

    protected abstract KeyManager[] engineGetKeyManagers();
}
