package java.io;

public class StringReader extends Reader {
    private String text;
    private int next;
    private int mark;

    public StringReader(String text) {
        if (text == null) {
            throw new NullPointerException("text");
        }
        this.text = text;
        next = 0;
        mark = 0;
    }

    private void ensureOpen() throws IOException {
        if (text == null) {
            throw new IOException("Stream closed");
        }
    }

    @Override
    public int read() throws IOException {
        ensureOpen();
        return next < text.length() ? text.charAt(next++) : -1;
    }

    @Override
    public int read(char[] target, int offset, int count)
            throws IOException {
        ensureOpen();
        if (offset < 0 || count < 0 ||
                offset > target.length - count) {
            throw new IndexOutOfBoundsException();
        }
        if (count == 0) return 0;
        if (next >= text.length()) return -1;
        int length = Math.min(count, text.length() - next);
        for (int index = 0; index < length; index++) {
            target[offset + index] = text.charAt(next + index);
        }
        next += length;
        return length;
    }

    @Override
    public long skip(long count) throws IOException {
        ensureOpen();
        if (next >= text.length()) return 0;
        long skipped = Math.min(count, text.length() - next);
        if (skipped < 0) return 0;
        next += (int) skipped;
        return skipped;
    }

    @Override
    public boolean ready() throws IOException {
        ensureOpen();
        return true;
    }

    @Override
    public boolean markSupported() {
        return true;
    }

    @Override
    public void mark(int readAheadLimit) throws IOException {
        ensureOpen();
        if (readAheadLimit < 0) {
            throw new IllegalArgumentException("Read-ahead limit < 0");
        }
        mark = next;
    }

    @Override
    public void reset() throws IOException {
        ensureOpen();
        next = mark;
    }

    @Override
    public void close() {
        text = null;
    }
}
