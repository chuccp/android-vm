package sun.security.provider;

/** HMAC-SHA-1（FIPS 198-1）；SUN provider "Mac.HmacSHA1" 服务。 */
public final class HmacSHA1 extends HmacCore {
    public HmacSHA1() {
        super("SHA-1", 20);
    }
}
