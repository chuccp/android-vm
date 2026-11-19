package java.util.zip;

import java.io.EOFException;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;

public class InflaterInputStream extends FilterInputStream {
    protected Inflater inf;
    protected byte[] buf;
    protected int len;
    protected InputStream source;
    boolean usesDefaultInflater;

    private boolean closed;
    private boolean eof;
    private final byte[] single = new byte[1];

    public InflaterInputStream(
            InputStream input, Inflater inflater, int size) {
        super(input);
        if (inflater == null) throw new NullPointerException("inflater");
        if (size <= 0) throw new IllegalArgumentException("buffer size <= 0");
        inf = inflater;
        source = input;
        buf = new byte[size];
    }

    public InflaterInputStream(InputStream input, Inflater inflater) {
        this(input, inflater, 512);
    }

    public InflaterInputStream(InputStream input) {
        this(input, new Inflater());
        usesDefaultInflater = true;
    }

    protected void fill() throws IOException {
        if (closed) throw new IOException("Stream closed");
        len = source.read(buf, 0, buf.length);
        if (len < 0) {
            throw new EOFException("Unexpected end of ZLIB input stream");
        }
        inf.setInput(buf, 0, len);
    }

    @Override
    public int read() throws IOException {
        return read(single, 0, 1) < 0
                ? -1
                : single[0] & 0xff;
    }

    @Override
    public int read(byte[] bytes, int offset, int length)
            throws IOException {
        if (closed) throw new IOException("Stream closed");
        if (bytes == null) throw new NullPointerException();
        if (offset < 0 || length < 0 ||
                offset > bytes.length - length) {
            throw new IndexOutOfBoundsException();
        }
        if (length == 0) return 0;
        try {
            while (true) {
                int count = inf.inflate(
                        bytes, offset, Math.min(length, 512));
                if (count > 0) return count;
                if (inf.finished() || inf.needsDictionary()) {
                    eof = true;
                    return -1;
                }
                if (inf.needsInput()) fill();
            }
        } catch (DataFormatException error) {
            throw new ZipException(
                    error.getMessage() == null
                            ? "Invalid ZLIB data format"
                            : error.getMessage());
        }
    }

    @Override
    public int available() throws IOException {
        if (closed) throw new IOException("Stream closed");
        return eof || inf.finished() ? 0 : 1;
    }

    @Override
    public long skip(long count) throws IOException {
        if (count < 0) throw new IllegalArgumentException();
        byte[] scratch = new byte[512];
        long skipped = 0;
        while (skipped < count) {
            int read = read(
                    scratch,
                    0,
                    (int) Math.min(scratch.length, count - skipped));
            if (read < 0) break;
            skipped += read;
        }
        return skipped;
    }

    @Override
    public void close() throws IOException {
        if (closed) return;
        if (usesDefaultInflater) inf.end();
        source.close();
        closed = true;
    }
}
