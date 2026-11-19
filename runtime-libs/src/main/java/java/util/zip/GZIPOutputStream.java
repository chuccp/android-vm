package java.util.zip;

import java.io.IOException;
import java.io.OutputStream;

public class GZIPOutputStream extends DeflaterOutputStream {
    protected CRC32 crc = new CRC32();

    private long inputSize;
    private boolean trailerWritten;

    public GZIPOutputStream(OutputStream output, int size)
            throws IOException {
        this(output, size, false);
    }

    public GZIPOutputStream(
            OutputStream output, int size, boolean syncFlush)
            throws IOException {
        super(output, new Deflater(Deflater.DEFAULT_COMPRESSION, true),
                size, syncFlush);
        usesDefaultDeflater = true;
        writeHeader();
    }

    public GZIPOutputStream(OutputStream output) throws IOException {
        this(output, DeflaterOutputStream.DEFAULT_BUF_SIZE, false);
    }

    public GZIPOutputStream(OutputStream output, boolean syncFlush)
            throws IOException {
        this(output, DeflaterOutputStream.DEFAULT_BUF_SIZE, syncFlush);
    }

    private void writeHeader() throws IOException {
        byte[] header = {
                0x1f, (byte) 0x8b, 8, 0,
                0, 0, 0, 0, 0, 0,
        };
        destination.write(header, 0, header.length);
    }

    @Override
    public synchronized void write(
            byte[] bytes, int offset, int length) throws IOException {
        super.write(bytes, offset, length);
        crc.update(bytes, offset, length);
        inputSize += length;
    }

    @Override
    public void finish() throws IOException {
        if (trailerWritten) return;
        super.finish();
        byte[] trailer = new byte[8];
        writeInt((int) crc.getValue(), trailer, 0);
        writeInt((int) inputSize, trailer, 4);
        destination.write(trailer, 0, trailer.length);
        trailerWritten = true;
    }

    @Override
    public void close() throws IOException {
        try {
            finish();
        } finally {
            super.close();
        }
    }

    private static void writeInt(int value, byte[] bytes, int offset) {
        bytes[offset] = (byte) value;
        bytes[offset + 1] = (byte) (value >>> 8);
        bytes[offset + 2] = (byte) (value >>> 16);
        bytes[offset + 3] = (byte) (value >>> 24);
    }
}
