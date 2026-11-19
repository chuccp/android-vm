package javax.net.ssl;

import java.security.PublicKey;

/**
 * 宿主 TLS 会话叶子证书公钥的 guest 视图（SPKI DER 由宿主提取；VM 不解析
 * 证书 DER——桥接优先口径）。CertificatePinner 等消费者只读取
 * getAlgorithm()/getEncoded()。
 */
final class BridgedPublicKey implements PublicKey {
    private static final long serialVersionUID = 1L;

    private final String algorithm;
    private final byte[] encoded;

    BridgedPublicKey(String algorithm, byte[] encoded) {
        this.algorithm = algorithm == null || algorithm.isEmpty() ? "UNKNOWN" : algorithm;
        this.encoded = encoded == null ? new byte[0] : encoded;
    }

    @Override
    public String getAlgorithm() {
        return algorithm;
    }

    @Override
    public String getFormat() {
        return "X.509";
    }

    @Override
    public byte[] getEncoded() {
        return encoded.clone();
    }

    @Override
    public String toString() {
        return "BridgedPublicKey[" + algorithm + "]";
    }
}
