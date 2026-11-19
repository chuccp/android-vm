package java.util.zip;

import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;

public class GZIPInputStream extends InflaterInputStream {
    public static final int GZIP_MAGIC = 0x8b1f;

    protected CRC32 crc = new CRC32();
    protected boolean eos;

    private boolean closed;
    private boolean bufferedTrailer;
    private int trailerOffset;
    private int trailerRemaining;

    public GZIPInputStream(InputStream input, int size)
            throws IOException {
        super(input, new Inflater(true), size);
        usesDefaultInflater = true;
        readHeader();
    }

    public GZIPInputStream(InputStream input) throws IOException {
        this(input, 512);
    }

    private int readByte(InputStream input) throws IOException {
        int value = input.read();
        if (value < 0) throw new EOFException();
        return value;
    }

    private void readHeader() throws IOException {
        if (readByte(source) != 0x1f ||
                readByte(source) != 0x8b) {
            throw new ZipException("Not in GZIP format");
        }
        if (readByte(source) != 8) {
            throw new ZipException("Unsupported compression method");
        }
        int flags = readByte(source);
        for (int index = 0; index < 6; index++) readByte(source);
        if ((flags & 4) != 0) {
            int length = readByte(source) | readByte(source) << 8;
            for (int index = 0; index < length; index++) readByte(source);
        }
        if ((flags & 8) != 0) {
            while (readByte(source) != 0) {
            }
        }
        if ((flags & 16) != 0) {
            while (readByte(source) != 0) {
            }
        }
        if ((flags & 2) != 0) {
            readByte(source);
            readByte(source);
        }
    }

    @Override
    public int read(byte[] bytes, int offset, int length)
            throws IOException {
        if (closed) throw new IOException("Stream closed");
        if (eos) return -1;
        int count = super.read(bytes, offset, length);
        if (count > 0) {
            crc.update(bytes, offset, count);
            return count;
        }
        verifyTrailer();
        eos = true;
        return -1;
    }

    private int nextTrailerByte() throws IOException {
        if (bufferedTrailer && trailerRemaining > 0) {
            trailerRemaining--;
            return buf[trailerOffset++] & 0xff;
        }
        return readByte(source);
    }

    private long readUInt() throws IOException {
        long low = nextTrailerByte();
        low |= (long) nextTrailerByte() << 8;
        low |= (long) nextTrailerByte() << 16;
        low |= (long) nextTrailerByte() << 24;
        return low & 0xffffffffL;
    }

    private void verifyTrailer() throws IOException {
        int remaining = inf.getRemaining();
        if (remaining > 0) {
            bufferedTrailer = true;
            trailerOffset = len - remaining;
            trailerRemaining = remaining;
        }
        if (readUInt() != crc.getValue() ||
                readUInt() != (inf.getBytesWritten() & 0xffffffffL)) {
            throw new ZipException("Corrupt GZIP trailer");
        }
    }

    @Override
    public void close() throws IOException {
        if (closed) return;
        super.close();
        eos = true;
        closed = true;
    }
}
