package sun.security.provider;

import java.io.Serializable;
import java.security.SecureRandomParameters;
import java.security.SecureRandomSpi;

/**
 * SHA1PRNG（SecureRandomSpi；§31.7 C 档 lib：OpenJDK 公开面形态）。
 *
 * <p>SHA1PRNG 状态机（OpenJDK sha1prng 确定性契约）：20 字节种子面 + 64 位
 * 计数器；输出块 = SHA1(seed || counter)，计数器小端 8 字节逐块递增，块内
 * 截取填充。确定性契约：首次输出前 {@code engineSetSeed} 任意次 → 同种子
 * 序列产生同输出流（SecureRandom(byte[]) 对照面）；无种启动 / 首输出后
 * setSeed 从 {@link SeedGenerator}（/dev/urandom）取熵混入：
 * {@code seed = SHA1(seed || newSeed)}。
 *
 * <p>MessageDigest 面（GetInstance 链）后置——SHA-1 自包含实现（FIPS 180-1），
 * 公开面对齐 OpenJDK sun.security.provider.SecureRandom（engineSetSeed /
 * engineNextBytes / engineGenerateSeed / engineReseed）。
 */
public final class SecureRandom extends SecureRandomSpi implements Serializable {

    private static final long serialVersionUID = -5662414345683267237L;

    private byte[] seed;
    private long counter;

    public SecureRandom() {
    }

    @Override
    protected synchronized void engineSetSeed(byte[] seedBytes) {
        if (seedBytes == null) {
            throw new NullPointerException("seedBytes == null");
        }
        if (seed == null) {
            seed = sha1(seedBytes);
        } else {
            byte[] in = new byte[seed.length + seedBytes.length];
            System.arraycopy(seed, 0, in, 0, seed.length);
            System.arraycopy(seedBytes, 0, in, seed.length, seedBytes.length);
            seed = sha1(in);
        }
    }

    @Override
    protected synchronized void engineNextBytes(byte[] bytes) {
        if (bytes == null) {
            throw new NullPointerException("bytes == null");
        }
        if (seed == null) {
            seed = SeedGenerator.generateSeed(20);
        }
        int off = 0;
        while (off < bytes.length) {
            byte[] block = sha1(concatCounter(seed, counter));
            counter++;
            int n = Math.min(block.length, bytes.length - off);
            System.arraycopy(block, 0, bytes, off, n);
            off += n;
        }
    }

    @Override
    protected byte[] engineGenerateSeed(int numBytes) {
        return SeedGenerator.generateSeed(numBytes);
    }

    @Override
    protected synchronized void engineReseed(SecureRandomParameters params) {
        if (params != null) {
            throw new UnsupportedOperationException("reseed(params)");
        }
        byte[] entropy = SeedGenerator.generateSeed(20);
        if (seed == null) {
            seed = sha1(entropy);
        } else {
            byte[] in = new byte[seed.length + entropy.length];
            System.arraycopy(seed, 0, in, 0, seed.length);
            System.arraycopy(entropy, 0, in, seed.length, entropy.length);
            seed = sha1(in);
        }
    }

    @Override
    public String toString() {
        return "SHA1PRNG SecureRandom";
    }

    private static byte[] concatCounter(byte[] seed, long counter) {
        byte[] in = new byte[seed.length + 8];
        System.arraycopy(seed, 0, in, 0, seed.length);
        for (int i = 0; i < 8; i++) {
            in[seed.length + i] = (byte) (counter >>> (8 * i));
        }
        return in;
    }

    // ---- SHA-1（FIPS 180-1，自包含）----
    private static byte[] sha1(byte[] message) {
        int[] h = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
        long bitLen = (long) message.length * 8;
        int padded = (message.length + 9 + 63) & ~63;
        byte[] block = new byte[padded];
        System.arraycopy(message, 0, block, 0, message.length);
        block[message.length] = (byte) 0x80;
        for (int i = 0; i < 8; i++) {
            block[padded - 1 - i] = (byte) (bitLen >>> (8 * i));
        }
        int[] w = new int[80];
        for (int off = 0; off < padded; off += 64) {
            for (int i = 0; i < 16; i++) {
                int j = off + i * 4;
                w[i] = ((block[j] & 0xff) << 24) | ((block[j + 1] & 0xff) << 16)
                        | ((block[j + 2] & 0xff) << 8) | (block[j + 3] & 0xff);
            }
            for (int i = 16; i < 80; i++) {
                w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            }
            int a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; i++) {
                int f, k;
                if (i < 20) {
                    f = (b & c) | (~b & d);
                    k = 0x5a827999;
                } else if (i < 40) {
                    f = b ^ c ^ d;
                    k = 0x6ed9eba1;
                } else if (i < 60) {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8f1bbcdc;
                } else {
                    f = b ^ c ^ d;
                    k = 0xca62c1d6;
                }
                int t = rotl(a, 5) + f + e + k + w[i];
                e = d;
                d = c;
                c = rotl(b, 30);
                b = a;
                a = t;
            }
            h[0] += a;
            h[1] += b;
            h[2] += c;
            h[3] += d;
            h[4] += e;
        }
        byte[] out = new byte[20];
        for (int i = 0; i < 5; i++) {
            out[i * 4] = (byte) (h[i] >>> 24);
            out[i * 4 + 1] = (byte) (h[i] >>> 16);
            out[i * 4 + 2] = (byte) (h[i] >>> 8);
            out[i * 4 + 3] = (byte) h[i];
        }
        return out;
    }

    private static int rotl(int v, int n) {
        return (v << n) | (v >>> (32 - n));
    }
}
