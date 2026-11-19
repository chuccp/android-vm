package javax.crypto.spec;

import java.security.spec.AlgorithmParameterSpec;
import java.util.Objects;

public final class ChaCha20ParameterSpec implements AlgorithmParameterSpec {

    private static final int NONCE_LENGTH = 12;

    private final byte[] nonce;
    private final int counter;

    public ChaCha20ParameterSpec(byte[] nonce, int counter) {
        this.counter = counter;

        Objects.requireNonNull(nonce, "Nonce must be non-null");
        this.nonce = nonce.clone();
        if (this.nonce.length != NONCE_LENGTH) {
            throw new IllegalArgumentException(
                    "Nonce must be 12-bytes in length");
        }
    }

    public byte[] getNonce() {
        return nonce.clone();
    }

    public int getCounter() {
        return counter;
    }
}
