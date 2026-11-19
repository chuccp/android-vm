package sectest;

import java.security.SecureRandom;
import java.util.Arrays;
import java.util.UUID;

/**
 * java.security 链探针：SecureRandom → Providers/SUN → SHA1PRNG（sun.security.provider）
 * → SeedGenerator /dev/urandom（zonfig UUID$Holder -> SecureRandom 链）。
 * 覆盖：无种启动真熵、SHA1PRNG setSeed 确定性契约（同种子同流、跨 nextBytes
 * 连续）、generateSeed 熵、getSeed 静面（seedGenerator）、UUID.randomUUID 链。
 */
public class SecureRandomProbe {

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    private static boolean anyNonZero(byte[] b) {
        for (int i = 0; i < b.length; i++) {
            if (b[i] != 0) {
                return true;
            }
        }
        return false;
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();
        try {
            SecureRandom r1 = new SecureRandom();
            byte[] b1 = new byte[32];
            r1.nextBytes(b1);
            sb.append(check(anyNonZero(b1), "sec-random-entropy"));
            SecureRandom r2 = new SecureRandom();
            byte[] b2 = new byte[32];
            r2.nextBytes(b2);
            sb.append(check(!Arrays.equals(b1, b2), "sec-random-fresh-differ"));
        } catch (Throwable t) {
            sb.append("sec-random-ctor: FAIL ").append(t).append('\n');
        }
        try {
            byte[] seed = new byte[] {1, 2, 3, 4, 5, 6, 7, 8};
            SecureRandom d1 = new SecureRandom(seed);
            SecureRandom d2 = new SecureRandom(seed);
            byte[] o1 = new byte[32];
            byte[] o2 = new byte[32];
            d1.nextBytes(o1);
            d2.nextBytes(o2);
            sb.append(check(Arrays.equals(o1, o2), "sec-sha1prng-determinism"));
            byte[] o3 = new byte[32];
            byte[] o4 = new byte[32];
            d1.nextBytes(o3);
            d2.nextBytes(o4);
            sb.append(check(Arrays.equals(o3, o4), "sec-sha1prng-stream"));
            sb.append(check(!Arrays.equals(o1, o3), "sec-sha1prng-advance"));
        } catch (Throwable t) {
            sb.append("sec-sha1prng: FAIL ").append(t).append('\n');
        }
        try {
            SecureRandom r = new SecureRandom();
            byte[] g1 = r.generateSeed(16);
            byte[] g2 = r.generateSeed(16);
            sb.append(check(g1.length == 16 && g2.length == 16 && !Arrays.equals(g1, g2),
                    "sec-generateSeed"));
            byte[] s = SecureRandom.getSeed(8);
            sb.append(check(s != null && s.length == 8, "sec-getSeed"));
        } catch (Throwable t) {
            sb.append("sec-seed: FAIL ").append(t).append('\n');
        }
        try {
            UUID u1 = UUID.randomUUID();
            UUID u2 = UUID.randomUUID();
            sb.append(check(u1 != null && u2 != null && !u1.equals(u2), "sec-uuid-random"));
        } catch (Throwable t) {
            sb.append("sec-uuid: FAIL ").append(t).append('\n');
        }
        return sb.toString();
    }
}
