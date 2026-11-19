package java.nio.channels;

import java.io.Closeable;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.MappedByteBuffer;

public class FileChannel implements SeekableByteChannel {
    public static final class MapMode {
        public static final MapMode READ_ONLY =
                new MapMode("READ_ONLY");
        public static final MapMode READ_WRITE =
                new MapMode("READ_WRITE");
        public static final MapMode PRIVATE =
                new MapMode("PRIVATE");

        private final String name;

        private MapMode(String name) {
            this.name = name;
        }

        @Override
        public String toString() {
            return name;
        }
    }

    private RandomAccessFile randomAccess;
    private boolean open = true;

    public FileChannel() {
    }

    public FileChannel(RandomAccessFile randomAccess) {
        this.randomAccess = randomAccess;
    }

    public void force(boolean metadata) throws IOException {
        requireOpen();
        if (randomAccess != null) {
            randomAccess.setLength(randomAccess.length());
        }
    }

    public long position() throws IOException {
        requireOpen();
        return randomAccess == null ? 0L : randomAccess.getFilePointer();
    }

    public FileChannel position(long newPosition) throws IOException {
        requireOpen();
        if (randomAccess != null) randomAccess.seek(newPosition);
        return this;
    }

    public long size() throws IOException {
        requireOpen();
        return randomAccess == null ? 0L : randomAccess.length();
    }

    public FileChannel truncate(long newSize) throws IOException {
        requireOpen();
        if (randomAccess != null) randomAccess.setLength(newSize);
        return this;
    }

    public MappedByteBuffer map(
            MapMode mode, long position, long size) throws IOException {
        requireOpen();
        if (randomAccess == null) {
            throw new IOException("Channel has no file");
        }
        if (mode == null) throw new NullPointerException("mode");
        if (position < 0 || size < 0 || size > Integer.MAX_VALUE) {
            throw new IllegalArgumentException();
        }
        return nativeMap(
                randomAccess.getPath(),
                mode != MapMode.READ_ONLY,
                position,
                size);
    }

    public FileLock lock() throws IOException {
        return lock(0L, Long.MAX_VALUE, false);
    }

    public FileLock lock(long position, long size, boolean shared)
            throws IOException {
        requireOpen();
        return nativeLock(position, size, shared);
    }

    public FileLock tryLock() throws IOException {
        return tryLock(0L, Long.MAX_VALUE, false);
    }

    public FileLock tryLock(
            long position, long size, boolean shared) throws IOException {
        requireOpen();
        return nativeLock(position, size, shared);
    }

    public int read(ByteBuffer destination) throws IOException {
        requireOpen();
        if (randomAccess == null) return -1;
        return transferRead(destination);
    }

    public int read(ByteBuffer destination, long position) throws IOException {
        requireOpen();
        if (randomAccess == null) return -1;
        long previous = randomAccess.getFilePointer();
        try {
            randomAccess.seek(position);
            return transferRead(destination);
        } finally {
            randomAccess.seek(previous);
        }
    }

    public int write(ByteBuffer source) throws IOException {
        requireOpen();
        if (randomAccess == null) return -1;
        return transferWrite(source);
    }

    public int write(ByteBuffer source, long position) throws IOException {
        requireOpen();
        if (randomAccess == null) return -1;
        long previous = randomAccess.getFilePointer();
        try {
            randomAccess.seek(position);
            return transferWrite(source);
        } finally {
            randomAccess.seek(previous);
        }
    }

    public long transferTo(
            long position, long count,
            java.nio.channels.WritableByteChannel target)
            throws IOException {
        requireOpen();
        long transferred = 0;
        while (transferred < count) {
            ByteBuffer buffer = ByteBuffer.allocate(
                    (int) Math.min(65536L, count - transferred));
            int read = read(buffer, position + transferred);
            if (read <= 0) break;
            buffer.flip();
            while (buffer.hasRemaining()) {
                int written = target.write(buffer);
                if (written <= 0) throw new IOException("Short transfer");
                transferred += written;
            }
        }
        return transferred;
    }

    public long transferFrom(
            java.nio.channels.ReadableByteChannel source,
            long position, long count) throws IOException {
        requireOpen();
        long transferred = 0;
        while (transferred < count) {
            ByteBuffer buffer = ByteBuffer.allocate(
                    (int) Math.min(65536L, count - transferred));
            int read = source.read(buffer);
            if (read <= 0) break;
            buffer.flip();
            while (buffer.hasRemaining()) {
                int written = write(buffer, position + transferred);
                if (written <= 0) throw new IOException("Short transfer");
                transferred += written;
            }
        }
        return transferred;
    }

    private int transferRead(ByteBuffer destination) throws IOException {
        if (!destination.hasRemaining()) return 0;
        int total = 0;
        while (destination.hasRemaining()) {
            byte[] bytes = new byte[
                    Math.min(destination.remaining(), 65536)];
            int count = randomAccess.read(bytes, 0, bytes.length);
            if (count <= 0) break;
            destination.put(bytes, 0, count);
            total += count;
            if (count < bytes.length) break;
        }
        return total == 0 ? -1 : total;
    }

    private int transferWrite(ByteBuffer source) throws IOException {
        if (!source.hasRemaining()) return 0;
        int total = 0;
        while (source.hasRemaining()) {
            byte[] bytes = new byte[
                    Math.min(source.remaining(), 65536)];
            int length = bytes.length;
            source.get(bytes, 0, length);
            randomAccess.write(bytes, 0, length);
            total += length;
        }
        return total;
    }

    public boolean isOpen() {
        return open;
    }

    @Override
    public void close() throws IOException {
        if (!open) return;
        open = false;
        if (randomAccess != null) randomAccess.close();
    }

    private void requireOpen() throws IOException {
        if (!open) throw new IOException("Channel closed");
    }

    private static native MappedByteBuffer nativeMap(
            String path, boolean writable, long position, long size);
    private native FileLock nativeLock(
            long position, long size, boolean shared);
}
