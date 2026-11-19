package java.util.zip;

import java.io.FilterOutputStream;
import java.io.IOException;
import java.io.OutputStream;

public class DeflaterOutputStream extends FilterOutputStream {
    static final int DEFAULT_BUF_SIZE = 512;

    protected Deflater def;
    protected byte[] buf;
    protected OutputStream destination;
    boolean usesDefaultDeflater;

    private final boolean syncFlush;
    public DeflaterOutputStream(
            OutputStream out,
            Deflater deflater,
            int size,
            boolean syncFlush) {
        super(out);
        if (deflater == null) throw new NullPointerException("deflater");
        if (size <= 0) throw new IllegalArgumentException("buffer size <= 0");
        def = deflater;
        destination = out;
        buf = new byte[size];
        this.syncFlush = syncFlush;
    }

    public DeflaterOutputStream(
            OutputStream out, Deflater deflater, int size) {
        this(out, deflater, size, false);
    }

    public DeflaterOutputStream(
            OutputStream out, Deflater deflater, boolean syncFlush) {
        this(out, deflater, DEFAULT_BUF_SIZE, syncFlush);
    }

    public DeflaterOutputStream(OutputStream out, Deflater deflater) {
        this(out, deflater, DEFAULT_BUF_SIZE, false);
    }

    public DeflaterOutputStream(OutputStream out, boolean syncFlush) {
        this(out, new Deflater(), DEFAULT_BUF_SIZE, syncFlush);
        usesDefaultDeflater = true;
    }

    public DeflaterOutputStream(OutputStream out) {
        this(out, false);
    }

    protected void deflate() throws IOException {
        int count = def.deflate(buf, 0, buf.length);
        if (count > 0) destination.write(buf, 0, count);
    }

    @Override
    public void write(int value) throws IOException {
        write(new byte[] {(byte) value}, 0, 1);
    }

    @Override
    public void write(byte[] bytes, int offset, int length)
            throws IOException {
        if (def.finished()) {
            throw new IOException("write beyond end of stream");
        }
        if (offset < 0 || length < 0 ||
                offset > bytes.length - length) {
            throw new IndexOutOfBoundsException();
        }
        if (length == 0) return;
        def.setInput(bytes, offset, length);
        while (!def.needsInput()) deflate();
    }

    public void finish() throws IOException {
        if (!def.finished()) {
            def.finish();
            while (!def.finished()) deflate();
        }
    }

    @Override
    public void flush() throws IOException {
        if (syncFlush && !def.finished()) {
            while (true) {
                int count = def.deflate(
                        buf, 0, buf.length, Deflater.SYNC_FLUSH);
                if (count > 0) destination.write(buf, 0, count);
                if (count < buf.length) break;
            }
        }
        destination.flush();
    }

    @Override
    public void close() throws IOException {
        try {
            finish();
        } finally {
            if (usesDefaultDeflater) def.end();
        }
        destination.close();
    }
}
