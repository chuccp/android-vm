package java.security.spec;

import java.util.Objects;

public class XECPrivateKeySpec implements KeySpec {

    private final AlgorithmParameterSpec params;
    private final byte[] scalar;

    public XECPrivateKeySpec(AlgorithmParameterSpec params, byte[] scalar) {
        Objects.requireNonNull(params, "params must not be null");
        Objects.requireNonNull(scalar, "scalar must not be null");

        this.params = params;
        this.scalar = scalar.clone();
    }

    public AlgorithmParameterSpec getParams() {
        return params;
    }

    public byte[] getScalar() {
        return scalar.clone();
    }
}
