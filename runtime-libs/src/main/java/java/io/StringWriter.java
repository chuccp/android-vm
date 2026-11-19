package java.io;

public class StringWriter extends Writer {
    private final StringBuffer buffer;

    public StringWriter() {
        this(16);
    }

    public StringWriter(int initialSize) {
        if (initialSize < 0) {
            throw new IllegalArgumentException("Negative buffer size");
        }
        buffer = new StringBuffer(initialSize);
    }

    @Override
    public void write(int character) {
        buffer.append((char) character);
    }

    @Override
    public void write(char[] characters, int offset, int count) {
        if (offset < 0 || count < 0 ||
                offset > characters.length - count) {
            throw new IndexOutOfBoundsException();
        }
        buffer.append(characters, offset, count);
    }

    @Override
    public void write(String text) {
        buffer.append(text);
    }

    @Override
    public void write(String text, int offset, int count) {
        buffer.append(text, offset, offset + count);
    }

    @Override
    public StringWriter append(CharSequence text) {
        buffer.append(text);
        return this;
    }

    @Override
    public StringWriter append(CharSequence text, int start, int end) {
        buffer.append(text, start, end);
        return this;
    }

    @Override
    public StringWriter append(char character) {
        buffer.append(character);
        return this;
    }

    @Override
    public String toString() {
        return buffer.toString();
    }

    public StringBuffer getBuffer() {
        return buffer;
    }

    @Override
    public void flush() {
    }

    @Override
    public void close() throws IOException {
    }
}
