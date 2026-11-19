package java.util.zip;

import java.nio.ByteBuffer;

public class CRC32 implements Checksum {
    private int value = -1;

    public CRC32() {
    }

    private static int update(int crc, int value) {
        crc ^= value & 0xff;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1) != 0
                    ? 0xedb88320 ^ (crc >>> 1)
                    : crc >>> 1;
        }
        return crc;
    }

    @Override
    public void update(int value) {
        this.value = update(this.value, value);
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
        int crc = value;
        for (int index = offset; index < offset + length; index++) {
            crc = update(crc, bytes[index]);
        }
        value = crc;
    }

    @Override
    public void update(ByteBuffer buffer) {
        while (buffer.hasRemaining()) update(buffer.get());
    }

    @Override
    public long getValue() {
        return (value ^ 0xffffffffL) & 0xffffffffL;
    }

    @Override
    public void reset() {
        value = -1;
    }
}
