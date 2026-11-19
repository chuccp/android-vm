package javax.net.ssl;

import java.math.BigInteger;
import java.security.InvalidKeyException;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.Principal;
import java.security.PublicKey;
import java.security.SignatureException;
import java.security.cert.CertificateEncodingException;
import java.security.cert.CertificateException;
import java.security.cert.CertificateParsingException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Date;
import java.util.List;

/**
 * 宿主 TLS 叶子证书的 guest 视图。
 *
 * 证书链校验已在宿主 TLS 栈完成（含 HTTPS endpoint identification），本类
 * 只把 OkHttp/OkHostnameVerifier 需要的身份面（SAN/主体 DN/公钥 SPKI）投影
 * 给 guest。DER 解析依赖的 sun.security.x509 在 lib 中不存在，故不实现
 * 编码/签名验证面（调用即抛 CertificateException）。
 */
final class BridgedX509Certificate extends X509Certificate {
    private static final long serialVersionUID = 1L;

    private final String subjectDn;
    private final String issuerDn;
    private final List<List<?>> subjectAltNames;
    private final BridgedPublicKey publicKey;

    BridgedX509Certificate(String subjectDn, String issuerDn,
                           List<List<?>> subjectAltNames, String keyAlgorithm,
                           byte[] keyEncoded) {
        this.subjectDn = subjectDn == null ? "" : subjectDn;
        this.issuerDn = issuerDn == null ? "" : issuerDn;
        this.subjectAltNames = subjectAltNames == null
                ? Collections.<List<?>>emptyList() : subjectAltNames;
        this.publicKey = new BridgedPublicKey(keyAlgorithm, keyEncoded);
    }

    @Override
    public Collection<List<?>> getSubjectAlternativeNames() throws CertificateParsingException {
        return subjectAltNames;
    }

    @Override
    public Collection<List<?>> getIssuerAlternativeNames() throws CertificateParsingException {
        return Collections.emptyList();
    }

    @Override
    public Principal getSubjectDN() {
        return new SimplePrincipal(subjectDn);
    }

    @Override
    public Principal getIssuerDN() {
        return new SimplePrincipal(issuerDn);
    }

    // X500Principal 的 lib 实现依赖 sun.security.x509.X500Name（lib 无此包），
    // 构造即 NoClassDefFoundError；OkHostnameVerifier 走 SAN 面，不需要 DN 对象。
    @Override
    public javax.security.auth.x500.X500Principal getSubjectX500Principal() {
        return null;
    }

    @Override
    public javax.security.auth.x500.X500Principal getIssuerX500Principal() {
        return null;
    }

    @Override
    public void checkValidity() {
    }

    @Override
    public void checkValidity(Date date) {
    }

    @Override
    public int getVersion() {
        return 3;
    }

    @Override
    public BigInteger getSerialNumber() {
        return BigInteger.ZERO;
    }

    @Override
    public Date getNotBefore() {
        return new Date(0L);
    }

    @Override
    public Date getNotAfter() {
        return new Date(253402300799000L);  // 9999-12-31
    }

    @Override
    public byte[] getTBSCertificate() throws CertificateEncodingException {
        throw new CertificateEncodingException("bridged certificate metadata only");
    }

    @Override
    public byte[] getSignature() {
        return new byte[0];
    }

    @Override
    public String getSigAlgName() {
        return "SHA256withRSA";
    }

    @Override
    public String getSigAlgOID() {
        return "1.2.840.113549.1.1.11";
    }

    @Override
    public byte[] getSigAlgParams() {
        return null;
    }

    @Override
    public boolean[] getIssuerUniqueID() {
        return null;
    }

    @Override
    public boolean[] getSubjectUniqueID() {
        return null;
    }

    @Override
    public boolean[] getKeyUsage() {
        return null;
    }

    @Override
    public List<String> getExtendedKeyUsage() throws CertificateParsingException {
        return Collections.emptyList();
    }

    @Override
    public int getBasicConstraints() {
        return -1;
    }

    @Override
    public byte[] getEncoded() throws CertificateEncodingException {
        throw new CertificateEncodingException("bridged certificate metadata only");
    }

    @Override
    public void verify(PublicKey key) throws CertificateException, NoSuchAlgorithmException,
            InvalidKeyException, NoSuchProviderException, SignatureException {
        throw new CertificateException("bridged certificate metadata only");
    }

    @Override
    public void verify(PublicKey key, String sigProvider) throws CertificateException,
            NoSuchAlgorithmException, InvalidKeyException, NoSuchProviderException,
            SignatureException {
        throw new CertificateException("bridged certificate metadata only");
    }

    @Override
    public PublicKey getPublicKey() {
        return publicKey;
    }

    @Override
    public boolean hasUnsupportedCriticalExtension() {
        return false;
    }

    @Override
    public java.util.Set<String> getCriticalExtensionOIDs() {
        return null;
    }

    @Override
    public java.util.Set<String> getNonCriticalExtensionOIDs() {
        return null;
    }

    @Override
    public byte[] getExtensionValue(String oid) {
        return null;
    }

    @Override
    public boolean equals(Object other) {
        return this == other;
    }

    @Override
    public int hashCode() {
        return System.identityHashCode(this);
    }

    @Override
    public String toString() {
        return "BridgedX509Certificate[" + subjectDn + "]";
    }

    private static final class SimplePrincipal implements Principal {
        private final String name;

        SimplePrincipal(String name) {
            this.name = name;
        }

        @Override
        public String getName() {
            return name;
        }

        @Override
        public String toString() {
            return name;
        }
    }

    static List<List<?>> emptySanList() {
        return new ArrayList<List<?>>();
    }
}
