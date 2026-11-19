package java.util.zip;

import java.nio.ByteBuffer;

public class Adler32 implements Checksum {
    private static final int MOD_ADLER = 65521;

    private int first = 1;
    private int second;

    public Adler32() {
    }

    @Override
    public void update(int value) {
        first = (first + (value & 0xff)) % MOD_ADLER;
        second = (second + first) % MOD_ADLER;
    }

    @Override
    public void update(byte[] bytes) {
        update(bytes, 0, bytes.length);
    }

    @Override
    public void update(byte[] bytes, int offset, int length) {
        if (bytes == null) throw new NullPointerException();
        if (offset < 0 || length < 0 || offset > bytes.length - length) {
            throw new ArrayIndexOutOfBoundsException();
        }
        int end = offset + length;
        while (offset < end) {
            int chunk = Math.min(5552, end - offset);
            for (int index = 0; index < chunk; index++) {
                first += bytes[offset++] & 0xff;
                second += first;
            }
            first %= MOD_ADLER;
            second %= MOD_ADLER;
        }
    }

    @Override
    public void update(ByteBuffer buffer) {
        while (buffer.hasRemaining()) update(buffer.get());
    }

    @Override
    public long getValue() {
        return ((long) second << 16) | (first & 0xffffL);
    }

    @Override
    public void reset() {
        first = 1;
        second = 0;
    }
}
