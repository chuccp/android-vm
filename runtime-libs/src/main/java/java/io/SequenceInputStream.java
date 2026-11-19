package java.io;

import java.util.Enumeration;

public class SequenceInputStream extends InputStream {
    private Enumeration<? extends InputStream> inputs;
    private InputStream current;
    private boolean closed;

    public SequenceInputStream(
            Enumeration<? extends InputStream> inputs) {
        if (inputs == null) throw new NullPointerException();
        this.inputs = inputs;
    }

    public SequenceInputStream(InputStream first, InputStream second) {
        this.inputs = new TwoStreams(first, second);
    }

    private void ensureOpen() throws IOException {
        if (closed) throw new IOException("Stream closed");
    }

    private boolean advance() {
        while (inputs != null && inputs.hasMoreElements()) {
            current = inputs.nextElement();
            if (current != null) return true;
        }
        current = null;
        return false;
    }

    @Override
    public int read() throws IOException {
        ensureOpen();
        while (current != null || advance()) {
            int value = current.read();
            if (value >= 0) return value;
            current.close();
            current = null;
        }
        return -1;
    }

    @Override
    public int read(byte[] target, int offset, int length)
            throws IOException {
        ensureOpen();
        if (target == null) throw new NullPointerException();
        if (offset < 0 || length < 0 ||
                offset > target.length - length) {
            throw new IndexOutOfBoundsException();
        }
        if (length == 0) return 0;
        while (current != null || advance()) {
            int count = current.read(target, offset, length);
            if (count > 0) return count;
            if (count < 0) {
                current.close();
                current = null;
            }
        }
        return -1;
    }

    @Override
    public int available() throws IOException {
        ensureOpen();
        if (current == null && !advance()) return 0;
        return current == null ? 0 : current.available();
    }

    @Override
    public void close() throws IOException {
        if (closed) return;
        closed = true;
        IOException failure = null;
        if (current != null) {
            try {
                current.close();
            } catch (IOException error) {
                failure = error;
            }
            current = null;
        }
        while (inputs != null && inputs.hasMoreElements()) {
            InputStream input = inputs.nextElement();
            if (input == null) continue;
            try {
                input.close();
            } catch (IOException error) {
                if (failure == null) failure = error;
            }
        }
        inputs = null;
        if (failure != null) throw failure;
    }

    private static final class TwoStreams
            implements Enumeration<InputStream> {
        private InputStream first;
        private InputStream second;

        TwoStreams(InputStream first, InputStream second) {
            this.first = first;
            this.second = second;
        }

        @Override
        public boolean hasMoreElements() {
            return first != null || second != null;
        }

        @Override
        public InputStream nextElement() {
            if (first != null) {
                InputStream value = first;
                first = null;
                return value;
            }
            if (second != null) {
                InputStream value = second;
                second = null;
                return value;
            }
            return null;
        }
    }
}
