package javax.crypto;

import java.security.*;
import java.security.spec.*;

public abstract class SecretKeyFactorySpi {

    protected abstract SecretKey engineGenerateSecret(KeySpec keySpec)
        throws InvalidKeySpecException;

    protected abstract KeySpec engineGetKeySpec(SecretKey key, Class<?> keySpec)
        throws InvalidKeySpecException;

    protected abstract SecretKey engineTranslateKey(SecretKey key)
        throws InvalidKeyException;
}
