package java.security.spec;

import java.math.BigInteger;
import java.util.Objects;

public class RSAMultiPrimePrivateCrtKeySpec extends RSAPrivateKeySpec {

    private final BigInteger publicExponent;
    private final BigInteger primeP;
    private final BigInteger primeQ;
    private final BigInteger primeExponentP;
    private final BigInteger primeExponentQ;
    private final BigInteger crtCoefficient;
    private final RSAOtherPrimeInfo[] otherPrimeInfo;

    public RSAMultiPrimePrivateCrtKeySpec(BigInteger modulus,
                                BigInteger publicExponent,
                                BigInteger privateExponent,
                                BigInteger primeP,
                                BigInteger primeQ,
                                BigInteger primeExponentP,
                                BigInteger primeExponentQ,
                                BigInteger crtCoefficient,
                                RSAOtherPrimeInfo[] otherPrimeInfo) {
        this(modulus, publicExponent, privateExponent, primeP, primeQ,
             primeExponentP, primeExponentQ, crtCoefficient, otherPrimeInfo,
             null);
    }

    public RSAMultiPrimePrivateCrtKeySpec(BigInteger modulus,
                                BigInteger publicExponent,
                                BigInteger privateExponent,
                                BigInteger primeP,
                                BigInteger primeQ,
                                BigInteger primeExponentP,
                                BigInteger primeExponentQ,
                                BigInteger crtCoefficient,
                                RSAOtherPrimeInfo[] otherPrimeInfo,
                                AlgorithmParameterSpec keyParams) {
        super(modulus, privateExponent, keyParams);
        Objects.requireNonNull(modulus,
            "the modulus parameter must be non-null");
        Objects.requireNonNull(privateExponent,
            "the privateExponent parameter must be non-null");
        this.publicExponent = Objects.requireNonNull(publicExponent,
            "the publicExponent parameter must be non-null");
        this.primeP = Objects.requireNonNull(primeP,
            "the primeP parameter must be non-null");
        this.primeQ = Objects.requireNonNull(primeQ,
            "the primeQ parameter must be non-null");
        this.primeExponentP = Objects.requireNonNull(primeExponentP,
            "the primeExponentP parameter must be non-null");
        this.primeExponentQ = Objects.requireNonNull(primeExponentQ,
            "the primeExponentQ parameter must be non-null");
        this.crtCoefficient = Objects.requireNonNull(crtCoefficient,
            "the crtCoefficient parameter must be non-null");

        if (otherPrimeInfo == null)  {
            this.otherPrimeInfo = null;
        } else if (otherPrimeInfo.length == 0) {
            throw new IllegalArgumentException("the otherPrimeInfo " +
                                                "parameter must not be empty");
        } else {
            this.otherPrimeInfo = otherPrimeInfo.clone();
        }
    }

    public BigInteger getPublicExponent() {
        return this.publicExponent;
    }

    public BigInteger getPrimeP() {
        return this.primeP;
    }

    public BigInteger getPrimeQ() {
        return this.primeQ;
    }

    public BigInteger getPrimeExponentP() {
        return this.primeExponentP;
    }

    public BigInteger getPrimeExponentQ() {
        return this.primeExponentQ;
    }

    public BigInteger getCrtCoefficient() {
        return this.crtCoefficient;
    }

    public RSAOtherPrimeInfo[] getOtherPrimeInfo() {
        if (otherPrimeInfo == null) return null;
        return otherPrimeInfo.clone();
    }
}
