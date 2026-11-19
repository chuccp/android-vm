package java.security.interfaces;

import java.math.BigInteger;
import java.security.spec.AlgorithmParameterSpec;

public interface RSAKey {

    public BigInteger getModulus();

    default AlgorithmParameterSpec getParams() {
        return null;
    }
}
