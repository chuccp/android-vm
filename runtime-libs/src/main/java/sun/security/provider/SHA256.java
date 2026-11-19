package sun.security.provider;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：SHA-256 MessageDigestSpi
 * （FIPS 180-4 摘要算法；OpenJDK sun.security.provider.SHA2$256 同算法语义，
 * 由 Sun provider "MessageDigest.SHA-256" 服务登记承接——okio
 * Buffer.sha256() 链消费面）。纯 Java 实现，无外部依赖。
 */
public final class SHA256 extends java.security.MessageDigestSpi {

    /** 轮常量（FIPS 180-4 前 64 个素数立方根小数部）。 */
    private static final int[] K = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };

    /** H0..H7（FIPS 180-4：前 8 个素数平方根小数部）。 */
    private static final int[] INITIAL = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };

    private final int[] state = new int[8];
    private final int[] w = new int[64];
    private final byte[] buffer = new byte[64];
    private int bufOfs;
    private long messageLength;

    public SHA256() {
        engineReset();
    }

    @Override
    protected int engineGetDigestLength() {
        return 32;
    }

    @Override
    protected void engineUpdate(byte input) {
        buffer[bufOfs++] = input;
        messageLength++;
        if (bufOfs == 64) {
            processBlock(buffer, 0);
            bufOfs = 0;
        }
    }

    @Override
    protected void engineUpdate(byte[] input, int offset, int len) {
        if (input == null) {
            throw new NullPointerException("input == null");
        }
        if (offset < 0 || len < 0 || offset > input.length - len) {
            throw new ArrayIndexOutOfBoundsException();
        }
        messageLength += len;
        while (len > 0) {
            int take = Math.min(len, 64 - bufOfs);
            System.arraycopy(input, offset, buffer, bufOfs, take);
            bufOfs += take;
            offset += take;
            len -= take;
            if (bufOfs == 64) {
                processBlock(buffer, 0);
                bufOfs = 0;
            }
        }
    }

    @Override
    protected byte[] engineDigest() {
        long bitLength = messageLength * 8L;
        // padding：0x80 + 零填充 + 64 位大端 bit 长度（FIPS 180-4；直写 buffer
        // 不经 engineUpdate——padding 不计消息长）
        buffer[bufOfs++] = (byte) 0x80;
        if (bufOfs > 56) {
            while (bufOfs < 64) buffer[bufOfs++] = 0;
            processBlock(buffer, 0);
            bufOfs = 0;
        }
        while (bufOfs < 56) buffer[bufOfs++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8) {
            buffer[bufOfs++] = (byte) (bitLength >>> shift);
        }
        processBlock(buffer, 0);
        bufOfs = 0;

        byte[] out = new byte[32];
        for (int i = 0; i < 8; i++) {
            out[i * 4] = (byte) (state[i] >>> 24);
            out[i * 4 + 1] = (byte) (state[i] >>> 16);
            out[i * 4 + 2] = (byte) (state[i] >>> 8);
            out[i * 4 + 3] = (byte) state[i];
        }
        engineReset();
        return out;
    }

    @Override
    protected void engineReset() {
        System.arraycopy(INITIAL, 0, state, 0, 8);
        bufOfs = 0;
        messageLength = 0;
    }

    /** 单块压缩（64 字节 @ ofs）。 */
    private void processBlock(byte[] block, int ofs) {
        for (int i = 0; i < 16; i++) {
            int j = ofs + i * 4;
            w[i] = ((block[j] & 0xff) << 24) | ((block[j + 1] & 0xff) << 16)
                    | ((block[j + 2] & 0xff) << 8) | (block[j + 3] & 0xff);
        }
        for (int i = 16; i < 64; i++) {
            int s0 = Integer.rotateRight(w[i - 15], 7)
                    ^ Integer.rotateRight(w[i - 15], 18) ^ (w[i - 15] >>> 3);
            int s1 = Integer.rotateRight(w[i - 2], 17)
                    ^ Integer.rotateRight(w[i - 2], 19) ^ (w[i - 2] >>> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        int a = state[0], b = state[1], c = state[2], d = state[3];
        int e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; i++) {
            int s1 = Integer.rotateRight(e, 6) ^ Integer.rotateRight(e, 11)
                    ^ Integer.rotateRight(e, 25);
            int ch = (e & f) ^ (~e & g);
            int t1 = h + s1 + ch + K[i] + w[i];
            int s0 = Integer.rotateRight(a, 2) ^ Integer.rotateRight(a, 13)
                    ^ Integer.rotateRight(a, 22);
            int maj = (a & b) ^ (a & c) ^ (b & c);
            int t2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
}
