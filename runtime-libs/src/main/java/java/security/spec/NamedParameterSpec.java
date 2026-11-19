package java.security.spec;

import java.util.Objects;

public class NamedParameterSpec implements AlgorithmParameterSpec {

    public static final NamedParameterSpec X25519
        = new NamedParameterSpec("X25519");

    public static final NamedParameterSpec X448
        = new NamedParameterSpec("X448");

    private String name;

    public NamedParameterSpec(String stdName) {
        Objects.requireNonNull(stdName, "stdName must not be null");

        this.name = stdName;
    }

    public String getName() {
        return name;
    }
}
