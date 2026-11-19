package java.util.zip;

import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;

public class ZipInputStream extends InflaterInputStream {
    private ZipEntry currentEntry;
    private byte[] currentData = new byte[0];
    private int position;
    private boolean closed;

    public ZipInputStream(InputStream input) {
        super(input, new Inflater(true));
        usesDefaultInflater = true;
    }

    public ZipEntry getNextEntry() throws IOException {
        closeEntry();
        int first = source.read();
        if (first < 0) return null;
        int signature =
                first |
                (readRequired() << 8) |
                (readRequired() << 16) |
                (readRequired() << 24);
        if (signature == 0x02014b50 || signature == 0x06054b50) {
            return null;
        }
        if (signature != 0x04034b50) {
            throw new ZipException("invalid local file header");
        }

        int version = readShort();
        int flags = readShort();
        int method = readShort();
        readShort();
        readShort();
        long crc = readInt() & 0xffffffffL;
        long compressedSize = readInt() & 0xffffffffL;
        long size = readInt() & 0xffffffffL;
        int nameLength = readShort();
        int extraLength = readShort();
        byte[] nameBytes = readBytes(nameLength);
        readBytes(extraLength);
        String name = new String(nameBytes);

        byte[] data;
        if (method == ZipEntry.STORED) {
            if ((flags & 8) != 0 || size < 0) {
                throw new ZipException(
                        "stored entry requires known size");
            }
            data = readBytes((int) size);
        } else if (method == ZipEntry.DEFLATED) {
            data = inflateEntry(
                    (flags & 8) != 0 ? -1L : compressedSize);
            if ((flags & 8) != 0) skipDescriptor();
        } else {
            throw new ZipException("unsupported compression method");
        }

        currentEntry = new ZipEntry(name);
        currentEntry.method = method;
        currentEntry.flag = flags;
        currentEntry.crc = crc;
        currentEntry.size = data.length;
        currentEntry.compressedSize = compressedSize;
        currentData = data;
        position = 0;
        return currentEntry;
    }

    private byte[] inflateEntry(long compressedSize) throws IOException {
        Inflater inflater = new Inflater(true);
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] outputBuffer = new byte[512];
        byte[] inputBuffer = new byte[512];
        try {
            if (compressedSize >= 0) {
                if (compressedSize > Integer.MAX_VALUE) {
                    throw new ZipException("entry too large");
                }
                byte[] compressed = readBytes((int) compressedSize);
                inflater.setInput(compressed);
                drain(inflater, output, outputBuffer);
            } else {
                while (!inflater.finished()) {
                    if (inflater.needsInput()) {
                        int count = source.read(
                                inputBuffer, 0, inputBuffer.length);
                        if (count < 0) {
                            throw new EOFException(
                                    "truncated compressed entry");
                        }
                        inflater.setInput(inputBuffer, 0, count);
                    }
                    int count = inflater.inflate(outputBuffer);
                    if (count > 0) {
                        output.write(outputBuffer, 0, count);
                    } else if (inflater.needsDictionary()) {
                        throw new ZipException("preset dictionary required");
                    }
                }
            }
        } catch (DataFormatException error) {
            throw new ZipException(
                    error.getMessage() == null
                            ? "invalid compressed entry"
                            : error.getMessage());
        } finally {
            inflater.end();
        }
        return output.toByteArray();
    }

    private void drain(
            Inflater inflater,
            ByteArrayOutputStream output,
            byte[] buffer) throws DataFormatException, IOException {
        while (!inflater.finished()) {
            int count = inflater.inflate(buffer);
            if (count > 0) {
                output.write(buffer, 0, count);
            } else if (inflater.needsDictionary()) {
                throw new ZipException("preset dictionary required");
            } else if (inflater.needsInput()) {
                throw new ZipException("truncated compressed entry");
            }
        }
    }

    private void skipDescriptor() throws IOException {
        int first = readRequired();
        if (first == 0x50) {
            byte[] rest = readBytes(3);
            if (rest[0] == 0x4b && rest[1] == 0x07 &&
                    rest[2] == 0x08) {
                readBytes(12);
                return;
            }
        }
        readBytes(11);
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
        if (currentEntry == null || position >= currentData.length) {
            return -1;
        }
        int count = Math.min(length, currentData.length - position);
        for (int index = 0; index < count; index++) {
            bytes[offset + index] = currentData[position + index];
        }
        position += count;
        return count;
    }

    @Override
    public int read() throws IOException {
        byte[] single = new byte[1];
        return read(single, 0, 1) < 0 ? -1 : single[0] & 0xff;
    }

    public void closeEntry() {
        if (currentEntry != null) position = currentData.length;
        currentEntry = null;
    }

    @Override
    public void close() throws IOException {
        if (closed) return;
        closeEntry();
        source.close();
        closed = true;
    }

    private int readRequired() throws IOException {
        int value = source.read();
        if (value < 0) throw new EOFException("truncated ZIP entry");
        return value;
    }

    private int readShort() throws IOException {
        return readRequired() | readRequired() << 8;
    }

    private int readInt() throws IOException {
        return readShort() | readShort() << 16;
    }

    private byte[] readBytes(int length) throws IOException {
        if (length < 0) throw new ZipException("invalid ZIP length");
        byte[] result = new byte[length];
        int offset = 0;
        while (offset < length) {
            int count = source.read(result, offset, length - offset);
            if (count < 0) throw new EOFException("truncated ZIP entry");
            offset += count;
        }
        return result;
    }
}
