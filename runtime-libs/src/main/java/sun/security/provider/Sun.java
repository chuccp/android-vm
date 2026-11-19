package sun.security.provider;

import java.security.Provider;

/**
 * SUN provider（§31.7 C 档 lib：OpenJDK 公开面形态）。
 *
 * <p>服务表按链上所需逐项落（SecureRandom.SHA1PRNG → 本包 SHA1PRNG 实现；
 * SunEntries 常量面 DEF_SECURE_RANDOM_ALGO=DRBG 未注册 → SecureRandom
 * getDefaultPRNG 走 SHA1PRNG 直建回退，OpenJDK 同款语义）。
 */
public final class Sun extends Provider {

    private static final long serialVersionUID = 6440182097789147485L;

    public Sun() {
        super("SUN", "1.0", "SUN (SHA1PRNG SecureRandom)");
        putService(new Service(this, "SecureRandom", "SHA1PRNG",
                "sun.security.provider.SecureRandom", null, null));
        // 摘要面（okio Buffer.sha256 / MessageDigest.getInstance 链）：
        // SHA-256（FIPS 180-4）、SHA-1（FIPS 180-4）、MD5（RFC 1321）
        putService(new Service(this, "MessageDigest", "SHA-256",
                "sun.security.provider.SHA256", null, null));
        putService(new Service(this, "MessageDigest", "SHA-1",
                "sun.security.provider.SHA1", null, null));
        putService(new Service(this, "MessageDigest", "MD5",
                "sun.security.provider.MD5", null, null));
        // HMAC 面（知乎 API 签名/云 ID 请求链）：HmacSHA256 / HmacSHA1
        putService(new Service(this, "Mac", "HmacSHA256",
                "sun.security.provider.HmacSHA256", null, null));
        putService(new Service(this, "Mac", "HmacSHA1",
                "sun.security.provider.HmacSHA1", null, null));
    }
}
