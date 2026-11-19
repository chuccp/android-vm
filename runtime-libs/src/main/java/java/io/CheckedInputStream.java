package java.io;

import java.util.zip.Checksum;

public class CheckedInputStream extends FilterInputStream {
    private final Checksum checksum;

    public CheckedInputStream(InputStream input, Checksum checksum) {
        super(input);
        if (checksum == null) throw new NullPointerException("checksum");
        this.checksum = checksum;
    }

    @Override
    public int read() throws IOException {
        int value = super.read();
        if (value >= 0) checksum.update(value);
        return value;
    }

    @Override
    public int read(byte[] bytes, int offset, int length)
            throws IOException {
        int count = super.read(bytes, offset, length);
        if (count > 0) checksum.update(bytes, offset, count);
        return count;
    }

    @Override
    public long skip(long count) throws IOException {
        byte[] buffer = new byte[512];
        long skipped = 0;
        while (skipped < count) {
            int read = read(
                    buffer,
                    0,
                    (int) Math.min(buffer.length, count - skipped));
            if (read < 0) break;
            skipped += read;
        }
        return skipped;
    }

    public Checksum getChecksum() {
        return checksum;
    }
}
