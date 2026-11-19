package sun.security.provider;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：SHA-1 MessageDigestSpi
 * （FIPS 180-4 摘要算法；OpenJDK sun.security.provider.SHA1 同算法语义，
 * 由 Sun provider "MessageDigest.SHA-1" 服务登记承接——
 * MessageDigest.getInstance("SHA-1") 消费面）。纯 Java 实现，无外部依赖。
 */
public final class SHA1 extends java.security.MessageDigestSpi {

    /** H0..H4（FIPS 180-4：前 5 个素数平方根小数部）。 */
    private final int[] state = new int[5];
    private final int[] w = new int[80];
    private final byte[] buffer = new byte[64];
    private int bufOfs;
    private long messageLength;

    public SHA1() {
        engineReset();
    }

    @Override
    protected int engineGetDigestLength() {
        return 20;
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

        byte[] out = new byte[20];
        for (int i = 0; i < 5; i++) {
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
        state[0] = 0x67452301;
        state[1] = 0xefcdab89;
        state[2] = 0x98badcfe;
        state[3] = 0x10325476;
        state[4] = 0xc3d2e1f0;
        bufOfs = 0;
        messageLength = 0;
    }

    private void processBlock(byte[] block, int off) {
        for (int i = 0; i < 16; i++) {
            w[i] = ((block[off + i * 4] & 0xff) << 24)
                    | ((block[off + i * 4 + 1] & 0xff) << 16)
                    | ((block[off + i * 4 + 2] & 0xff) << 8)
                    | (block[off + i * 4 + 3] & 0xff);
        }
        for (int i = 16; i < 80; i++) {
            w[i] = Integer.rotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        int a = state[0];
        int b = state[1];
        int c = state[2];
        int d = state[3];
        int e = state[4];
        for (int i = 0; i < 80; i++) {
            int f;
            int k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
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
            int t = Integer.rotateLeft(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = Integer.rotateLeft(b, 30);
            b = a;
            a = t;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }
}
