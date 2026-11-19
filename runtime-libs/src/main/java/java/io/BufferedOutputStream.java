package java.io;

public class BufferedOutputStream extends FilterOutputStream {
    private byte[] buffer;
    private int count;

    public BufferedOutputStream(OutputStream out) {
        this(out, 8192);
    }

    public BufferedOutputStream(OutputStream out, int size) {
        super(out);
        if (size <= 0) throw new IllegalArgumentException();
        buffer = new byte[size];
    }

    private void flushBuffer() throws IOException {
        if (count == 0) return;
        out.write(buffer, 0, count);
        count = 0;
    }

    @Override
    public synchronized void write(int value) throws IOException {
        if (count == buffer.length) flushBuffer();
        buffer[count++] = (byte) value;
    }

    @Override
    public synchronized void write(
            byte[] source, int offset, int length) throws IOException {
        if (offset < 0 || length < 0 ||
                offset + length > source.length) {
            throw new IndexOutOfBoundsException();
        }
        if (length >= buffer.length) {
            flushBuffer();
            out.write(source, offset, length);
            return;
        }
        if (length > buffer.length - count) flushBuffer();
        System.arraycopy(source, offset, buffer, count, length);
        count += length;
    }

    @Override
    public synchronized void flush() throws IOException {
        flushBuffer();
        out.flush();
    }

    @Override
    public synchronized void close() throws IOException {
        flush();
        out.close();
        buffer = new byte[0];
        count = 0;
    }
}
