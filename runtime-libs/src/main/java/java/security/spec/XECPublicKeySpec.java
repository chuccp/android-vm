package java.security.spec;

import java.math.BigInteger;
import java.util.Objects;

public class XECPublicKeySpec implements KeySpec {

    private final AlgorithmParameterSpec params;
    private final BigInteger u;

    public XECPublicKeySpec(AlgorithmParameterSpec params, BigInteger u) {
        Objects.requireNonNull(params, "params must not be null");
        Objects.requireNonNull(u, "u must not be null");

        this.params = params;
        this.u = u;
    }

    public AlgorithmParameterSpec getParams() {
        return params;
    }

    public BigInteger getU() {
        return u;
    }
}
