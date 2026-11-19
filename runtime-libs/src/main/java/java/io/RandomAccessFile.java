package java.io;

import java.nio.channels.FileChannel;

public class RandomAccessFile implements Closeable {
    private String path;
    private long position;
    private boolean readable;
    private boolean writable;
    private boolean closed;
    // getFD() 惰性创建的宿主 fd 持有面：seek() 需要 lseek 同一个打开文件
    // 描述，FileOutputStream(getFD()) 的 write 才会落到正确偏移
    // （FileDownloadRandomAccessFile 多连接分段下载依赖此语义）。
    private FileDescriptor fd;

    public RandomAccessFile(String name, String mode) throws IOException {
        this.path = new File(name).getPath();
        configureMode(mode);
        if (!nativePrepare(path, writable)) {
            throw new FileNotFoundException(path);
        }
    }

    public RandomAccessFile(File file, String mode) throws IOException {
        this(file.getPath(), mode);
    }

    private void configureMode(String mode) {
        if (mode == null) throw new NullPointerException("mode");
        readable = "r".equals(mode) || "rw".equals(mode) ||
                "rws".equals(mode) || "rwd".equals(mode);
        writable = "rw".equals(mode) || "rws".equals(mode) ||
                "rwd".equals(mode);
        if (!readable && !writable) {
            throw new IllegalArgumentException("Illegal mode: " + mode);
        }
    }

    private void ensureOpen() throws IOException {
        if (closed) throw new IOException("Stream Closed");
    }

    private void ensureReadable() throws IOException {
        ensureOpen();
        if (!readable) throw new IOException("Read access denied");
    }

    private void ensureWritable() throws IOException {
        ensureOpen();
        if (!writable) throw new IOException("Write access denied");
    }

    public synchronized int read() throws IOException {
        byte[] value = new byte[1];
        return read(value, 0, 1) < 0 ? -1 : value[0] & 0xff;
    }

    public synchronized int read(byte[] target) throws IOException {
        return read(target, 0, target.length);
    }

    public synchronized int read(
            byte[] target, int offset, int length) throws IOException {
        ensureReadable();
        if (offset < 0 || length < 0 ||
                offset + length > target.length) {
            throw new IndexOutOfBoundsException();
        }
        int count = nativeReadAt(path, position, target, offset, length);
        if (count > 0) position += count;
        return count;
    }

    public synchronized void readFully(byte[] target) throws IOException {
        readFully(target, 0, target.length);
    }

    public synchronized void readFully(
            byte[] target, int offset, int length) throws IOException {
        int completed = 0;
        while (completed < length) {
            int read = read(target, offset + completed, length - completed);
            if (read < 0) throw new EOFException();
            completed += read;
        }
    }

    public synchronized int skipBytes(int count) throws IOException {
        ensureOpen();
        if (count <= 0) return 0;
        long target = Math.min(length(), position + count);
        int skipped = (int) (target - position);
        position = target;
        return skipped;
    }

    public synchronized void seek(long newPosition) throws IOException {
        ensureOpen();
        if (newPosition < 0) throw new IOException("Negative seek offset");
        position = newPosition;
        FileDescriptor current = fd;
        if (current != null) {
            nativeSeekFd(current, newPosition);
        }
    }

    public synchronized long getFilePointer() throws IOException {
        ensureOpen();
        return position;
    }

    public synchronized long length() throws IOException {
        ensureOpen();
        long length = nativeSize(path);
        if (length < 0) throw new FileNotFoundException(path);
        return length;
    }

    public synchronized FileChannel getChannel() {
        return new FileChannel(this);
    }

    public final FileDescriptor getFD() throws IOException {
        ensureOpen();
        if (fd == null) {
            fd = nativeGetFD(path, writable);
        }
        return fd;
    }

    public final String readLine() throws IOException {
        ensureReadable();
        StringBuilder line = new StringBuilder();
        int character;
        while ((character = read()) >= 0) {
            if (character == '\n') break;
            if (character == '\r') {
                int next = read();
                if (next >= 0 && next != '\n') seek(position - 1);
                break;
            }
            line.append((char) character);
        }
        return character < 0 && line.length() == 0
                ? null
                : line.toString();
    }

    public String getPath() {
        return path;
    }

    public synchronized void setLength(long newLength) throws IOException {
        ensureWritable();
        if (newLength < 0) throw new IllegalArgumentException();
        nativeSetLength(path, newLength);
        position = Math.min(position, newLength);
    }

    public synchronized void write(int value) throws IOException {
        write(new byte[] {(byte) value}, 0, 1);
    }

    public synchronized void write(byte[] source) throws IOException {
        write(source, 0, source.length);
    }

    public synchronized void write(
            byte[] source, int offset, int length) throws IOException {
        ensureWritable();
        if (offset < 0 || length < 0 ||
                offset + length > source.length) {
            throw new IndexOutOfBoundsException();
        }
        int count = nativeWriteAt(path, position, source, offset, length);
        if (count != length) throw new IOException("Short write");
        position += count;
    }

    public synchronized void writeBoolean(boolean value) throws IOException {
        write(value ? 1 : 0);
    }

    public synchronized void writeByte(int value) throws IOException {
        write(value);
    }

    public synchronized void writeShort(int value) throws IOException {
        write(value >>> 8);
        write(value);
    }

    public synchronized void writeInt(int value) throws IOException {
        write(value >>> 24);
        write(value >>> 16);
        write(value >>> 8);
        write(value);
    }

    public synchronized void writeLong(long value) throws IOException {
        writeInt((int) (value >>> 32));
        writeInt((int) value);
    }

    public synchronized void writeBytes(String value) throws IOException {
        for (int index = 0; index < value.length(); index++) {
            write((byte) value.charAt(index));
        }
    }

    public synchronized void writeChars(String value) throws IOException {
        for (int index = 0; index < value.length(); index++) {
            writeShort(value.charAt(index));
        }
    }

    public synchronized void writeUTF(String value) throws IOException {
        byte[] encoded = value.getBytes();
        if (encoded.length > 65535) {
            throw new UTFDataFormatException();
        }
        writeShort(encoded.length);
        write(encoded);
    }

    public synchronized boolean readBoolean() throws IOException {
        return read() != 0;
    }

    public synchronized byte readByte() throws IOException {
        return (byte) readRequired();
    }

    public synchronized int readUnsignedByte() throws IOException {
        return readRequired();
    }

    public synchronized short readShort() throws IOException {
        return (short) ((readRequired() << 8) | readRequired());
    }

    public synchronized int readUnsignedShort() throws IOException {
        return (readRequired() << 8) | readRequired();
    }

    public synchronized int readInt() throws IOException {
        return (readRequired() << 24) |
                (readRequired() << 16) |
                (readRequired() << 8) |
                readRequired();
    }

    public synchronized long readLong() throws IOException {
        return ((long) readInt() << 32) |
                (readInt() & 0xffffffffL);
    }

    public synchronized String readUTF() throws IOException {
        int length = readUnsignedShort();
        byte[] encoded = new byte[length];
        readFully(encoded);
        return new String(encoded);
    }

    private int readRequired() throws IOException {
        int value = read();
        if (value < 0) throw new EOFException();
        return value;
    }

    @Override
    public synchronized void close() throws IOException {
        closed = true;
    }

    private static native boolean nativePrepare(String path, boolean writable);
    private static native FileDescriptor nativeGetFD(String path, boolean writable);
    private static native void nativeSeekFd(FileDescriptor fd, long position)
            throws IOException;
    private static native long nativeSize(String path);
    private static native int nativeReadAt(
            String path, long position, byte[] target,
            int offset, int length);
    private static native int nativeWriteAt(
            String path, long position, byte[] source,
            int offset, int length);
    private static native void nativeSetLength(String path, long length);
}
