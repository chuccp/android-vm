package java.security.spec;

public abstract class EncodedKeySpec implements KeySpec {

    private byte[] encodedKey;
    private String algorithmName;

    public EncodedKeySpec(byte[] encodedKey) {
        this.encodedKey = encodedKey.clone();
    }

    protected EncodedKeySpec(byte[] encodedKey, String algorithm) {
        if (algorithm == null) {
            throw new NullPointerException("algorithm name may not be null");
        }
        if (algorithm.isEmpty()) {
            throw new IllegalArgumentException("algorithm name "
                                             + "may not be empty");
        }
        this.encodedKey = encodedKey.clone();
        this.algorithmName = algorithm;

    }

    public String getAlgorithm() {
        return algorithmName;
    }

    public byte[] getEncoded() {
        return this.encodedKey.clone();
    }

    public abstract String getFormat();
}
