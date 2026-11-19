package jdk.internal.misc;

public interface JavaSecuritySignatureAccess {
    void initVerify(java.security.Signature s, java.security.PublicKey publicKey,
                    java.security.spec.AlgorithmParameterSpec params)
            throws java.security.InvalidKeyException,
                    java.security.InvalidAlgorithmParameterException;
    void initVerify(java.security.Signature s, java.security.cert.Certificate certificate,
                    java.security.spec.AlgorithmParameterSpec params)
            throws java.security.InvalidKeyException,
                    java.security.InvalidAlgorithmParameterException;
    void initSign(java.security.Signature s, java.security.PrivateKey privateKey,
                  java.security.spec.AlgorithmParameterSpec params,
                  java.security.SecureRandom random)
            throws java.security.InvalidKeyException,
                    java.security.InvalidAlgorithmParameterException;
}
