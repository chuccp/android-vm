package java.io;

import java.util.zip.Checksum;

public class CheckedOutputStream extends FilterOutputStream {
    private final Checksum checksum;

    public CheckedOutputStream(OutputStream output, Checksum checksum) {
        super(output);
        if (checksum == null) throw new NullPointerException("checksum");
        this.checksum = checksum;
    }

    @Override
    public void write(int value) throws IOException {
        out.write(value);
        checksum.update(value);
    }

    @Override
    public void write(byte[] bytes, int offset, int length)
            throws IOException {
        out.write(bytes, offset, length);
        checksum.update(bytes, offset, length);
    }

    public Checksum getChecksum() {
        return checksum;
    }
}
