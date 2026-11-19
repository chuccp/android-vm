package java.io;

public class FilterOutputStream extends OutputStream {
    protected OutputStream out;

    public FilterOutputStream(OutputStream out) {
        if (out == null) throw new NullPointerException("out");
        this.out = out;
    }

    @Override
    public void write(int value) throws IOException {
        out.write(value);
    }

    @Override
    public void write(byte[] source, int offset, int length)
            throws IOException {
        out.write(source, offset, length);
    }

    @Override
    public void flush() throws IOException {
        out.flush();
    }

    @Override
    public void close() throws IOException {
        out.close();
    }
}
