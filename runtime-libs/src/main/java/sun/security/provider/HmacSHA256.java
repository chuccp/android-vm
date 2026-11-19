package sun.security.provider;

/** HMAC-SHA-256（FIPS 198-1）；SUN provider "Mac.HmacSHA256" 服务。 */
public final class HmacSHA256 extends HmacCore {
    public HmacSHA256() {
        super("SHA-256", 32);
    }
}
