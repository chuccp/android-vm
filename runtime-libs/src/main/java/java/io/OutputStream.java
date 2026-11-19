package java.io;

public abstract class OutputStream implements Closeable {
    public abstract void write(int value) throws IOException;

    public void write(byte[] source) throws IOException {
        write(source, 0, source.length);
    }

    public void write(byte[] source, int offset, int length)
            throws IOException {
        if (offset < 0 || length < 0 ||
                offset + length > source.length) {
            throw new IndexOutOfBoundsException();
        }
        for (int index = 0; index < length; index++) {
            write(source[offset + index]);
        }
    }

    public void flush() throws IOException {
    }

    @Override
    public void close() throws IOException {
        flush();
    }
}
