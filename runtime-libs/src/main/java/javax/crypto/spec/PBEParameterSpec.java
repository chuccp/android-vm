package javax.crypto.spec;

import java.security.spec.AlgorithmParameterSpec;

public class PBEParameterSpec implements AlgorithmParameterSpec {

    private byte[] salt;
    private int iterationCount;
    private AlgorithmParameterSpec paramSpec = null;

    public PBEParameterSpec(byte[] salt, int iterationCount) {
        this.salt = salt.clone();
        this.iterationCount = iterationCount;
    }

    public PBEParameterSpec(byte[] salt, int iterationCount,
            AlgorithmParameterSpec paramSpec) {
        this.salt = salt.clone();
        this.iterationCount = iterationCount;
        this.paramSpec = paramSpec;
    }

    public byte[] getSalt() {
        return this.salt.clone();
    }

    public int getIterationCount() {
        return this.iterationCount;
    }

    public AlgorithmParameterSpec getParameterSpec() {
        return this.paramSpec;
    }
}
