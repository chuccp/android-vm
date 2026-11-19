package sun.security.provider;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：MD5 MessageDigestSpi
 * （RFC 1321 摘要算法；OpenJDK sun.security.provider.MD5 同算法语义，
 * 由 Sun provider "MessageDigest.MD5" 服务登记承接——
 * MessageDigest.getInstance("MD5") 消费面）。纯 Java 实现，无外部依赖。
 */
public final class MD5 extends java.security.MessageDigestSpi {

    /** 轮常量（RFC 1321：sin 绝对值小数部 × 2^32；K[i] = floor(|sin(i+1)|·2^32）。 */
    private static final int[] K = new int[64];

    /** 每轮左移位数（RFC 1321 S 表）。 */
    private static final int[] S = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
    };

    static {
        for (int i = 0; i < 64; i++) {
            K[i] = (int) (long) Math.floor(Math.abs(Math.sin(i + 1)) * 4294967296.0);
        }
    }

    private final int[] state = new int[4];
    private final int[] x = new int[16];
    private final byte[] buffer = new byte[64];
    private int bufOfs;
    private long messageLength;

    public MD5() {
        engineReset();
    }

    @Override
    protected int engineGetDigestLength() {
        return 16;
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
        // padding：0x80 + 零填充 + 64 位小端 bit 长度（RFC 1321；直写 buffer
        // 不经 engineUpdate——padding 不计消息长）
        buffer[bufOfs++] = (byte) 0x80;
        if (bufOfs > 56) {
            while (bufOfs < 64) buffer[bufOfs++] = 0;
            processBlock(buffer, 0);
            bufOfs = 0;
        }
        while (bufOfs < 56) buffer[bufOfs++] = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            buffer[bufOfs++] = (byte) (bitLength >>> shift);
        }
        processBlock(buffer, 0);
        bufOfs = 0;

        byte[] out = new byte[16];
        for (int i = 0; i < 4; i++) {
            out[i * 4] = (byte) state[i];
            out[i * 4 + 1] = (byte) (state[i] >>> 8);
            out[i * 4 + 2] = (byte) (state[i] >>> 16);
            out[i * 4 + 3] = (byte) (state[i] >>> 24);
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
        bufOfs = 0;
        messageLength = 0;
    }

    private void processBlock(byte[] block, int off) {
        for (int i = 0; i < 16; i++) {
            x[i] = (block[off + i * 4] & 0xff)
                    | ((block[off + i * 4 + 1] & 0xff) << 8)
                    | ((block[off + i * 4 + 2] & 0xff) << 16)
                    | ((block[off + i * 4 + 3] & 0xff) << 24);
        }
        int a = state[0];
        int b = state[1];
        int c = state[2];
        int d = state[3];
        for (int i = 0; i < 64; i++) {
            int f;
            int g;
            if (i < 16) {
                f = (b & c) | ((~b) & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | ((~d) & c);
                g = (5 * i + 1) & 15;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) & 15;
            } else {
                f = c ^ (b | (~d));
                g = (7 * i) & 15;
            }
            int tmp = d;
            d = c;
            c = b;
            int sum = a + f + K[i] + x[g];
            b = b + Integer.rotateLeft(sum, S[i]);
            a = tmp;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
    }
}
