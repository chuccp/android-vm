package java.util.zip;

import java.io.Closeable;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.charset.Charset;
import java.util.Enumeration;
import java.util.NoSuchElementException;

public class ZipFile implements Closeable {
    public static final int OPEN_READ = 0x1;
    public static final int OPEN_DELETE = 0x4;

    private static final int EOCD_SIGNATURE = 0x06054b50;
    private static final int CENTRAL_SIGNATURE = 0x02014b50;
    private static final int LOCAL_SIGNATURE = 0x04034b50;

    private static final class Record {
        ZipEntry entry;
        long localOffset;
    }

    private final File file;
    private final String name;
    private final boolean deleteOnClose;
    private Record[] records = new Record[16];
    private int recordCount;
    private byte[] comment;
    private boolean closed;

    public ZipFile(String name) throws IOException {
        this(new File(name), OPEN_READ);
    }

    public ZipFile(String name, Charset charset) throws IOException {
        this(new File(name), OPEN_READ);
        if (charset == null) throw new NullPointerException("charset");
    }

    public ZipFile(File file) throws IOException {
        this(file, OPEN_READ);
    }

    public ZipFile(File file, int mode) throws IOException {
        if (file == null) throw new NullPointerException("file");
        if ((mode & OPEN_READ) == 0 ||
                (mode & ~(OPEN_READ | OPEN_DELETE)) != 0) {
            throw new IllegalArgumentException("invalid mode");
        }
        this.file = file;
        this.name = file.getPath();
        this.deleteOnClose = (mode & OPEN_DELETE) != 0;
        readCentralDirectory();
    }

    public ZipFile(File file, Charset charset) throws IOException {
        this(file, OPEN_READ);
        if (charset == null) throw new NullPointerException("charset");
    }

    public ZipFile(File file, int mode, Charset charset)
            throws IOException {
        this(file, mode);
        if (charset == null) throw new NullPointerException("charset");
    }

    private void readCentralDirectory() throws IOException {
        RandomAccessFile input = new RandomAccessFile(file, "r");
        try {
            long length = input.length();
            if (length < 22L) throw new ZipException("zip file is too small");
            int tailLength = (int) Math.min(length, 65557L);
            byte[] tail = new byte[tailLength];
            input.seek(length - tailLength);
            input.readFully(tail);
            int eocd = -1;
            for (int index = tail.length - 22; index >= 0; index--) {
                if (readInt(tail, index) == EOCD_SIGNATURE) {
                    eocd = index;
                    break;
                }
            }
            if (eocd < 0) throw new ZipException("end of central directory not found");
            int count = readShort(tail, eocd + 10);
            long centralSize = readUnsignedInt(tail, eocd + 12);
            long centralOffset = readUnsignedInt(tail, eocd + 16);
            int commentLength = readShort(tail, eocd + 20);
            comment = new byte[commentLength];
            for (int index = 0; index < commentLength; index++) {
                comment[index] = tail[eocd + 22 + index];
            }
            if (count >= 0xffff || centralSize >= 0xffffffffL ||
                    centralOffset >= 0xffffffffL) {
                throw new ZipException("ZIP64 archives are not supported");
            }
            input.seek(centralOffset);
            for (int index = 0; index < count; index++) {
                byte[] fixed = new byte[46];
                input.readFully(fixed);
                if (readInt(fixed, 0) != CENTRAL_SIGNATURE) {
                    throw new ZipException("invalid central directory");
                }
                int nameLength = readShort(fixed, 28);
                int extraLength = readShort(fixed, 30);
                int entryCommentLength = readShort(fixed, 32);
                byte[] nameBytes = new byte[nameLength];
                input.readFully(nameBytes);
                byte[] extra = new byte[extraLength];
                input.readFully(extra);
                if (entryCommentLength > 0) {
                    input.readFully(new byte[entryCommentLength]);
                }

                ZipEntry entry = new ZipEntry(new String(nameBytes));
                entry.method = readShort(fixed, 10);
                entry.flag = readShort(fixed, 8);
                entry.crc = readUnsignedInt(fixed, 16);
                entry.compressedSize = readUnsignedInt(fixed, 20);
                entry.size = readUnsignedInt(fixed, 24);
                entry.extra = extra;
                Record record = new Record();
                record.entry = entry;
                record.localOffset = readUnsignedInt(fixed, 42);
                append(record);
            }
        } finally {
            input.close();
        }
    }

    private void append(Record record) {
        if (recordCount == records.length) {
            Record[] grown = new Record[records.length * 2];
            for (int index = 0; index < records.length; index++) {
                grown[index] = records[index];
            }
            records = grown;
        }
        records[recordCount++] = record;
    }

    public String getName() {
        return name;
    }

    public String getComment() {
        return comment == null ? null : new String(comment);
    }

    public int size() {
        return recordCount;
    }

    public ZipEntry getEntry(String entryName) {
        if (entryName == null) throw new NullPointerException("name");
        for (int index = 0; index < recordCount; index++) {
            ZipEntry entry = records[index].entry;
            if (entry.getName().equals(entryName)) return entry;
        }
        return null;
    }

    public Enumeration<? extends ZipEntry> entries() {
        ensureOpen();
        return new Enumeration<ZipEntry>() {
            private int index;

            @Override
            public boolean hasMoreElements() {
                return index < recordCount;
            }

            @Override
            public ZipEntry nextElement() {
                if (!hasMoreElements()) throw new NoSuchElementException();
                return records[index++].entry;
            }
        };
    }

    public InputStream getInputStream(ZipEntry entry)
            throws IOException {
        ensureOpen();
        if (entry == null) throw new NullPointerException("entry");
        for (int index = 0; index < recordCount; index++) {
            Record record = records[index];
            if (record.entry.getName().equals(entry.getName())) {
                return new EntryStream(record);
            }
        }
        throw new ZipException("entry not found: " + entry.getName());
    }

    @Override
    public void close() throws IOException {
        if (closed) return;
        closed = true;
        if (deleteOnClose) file.delete();
    }

    private void ensureOpen() {
        if (closed) throw new IllegalStateException("ZipFile closed");
    }

    private final class EntryStream extends InputStream {
        private final Record record;
        private byte[] data;
        private int position;

        EntryStream(Record record) {
            this.record = record;
        }

        private void load() throws IOException {
            if (data != null) return;
            if (closed) throw new IOException("ZipFile closed");
            ZipEntry entry = record.entry;
            RandomAccessFile input = new RandomAccessFile(file, "r");
            byte[] compressed;
            try {
                input.seek(record.localOffset);
                byte[] local = new byte[30];
                input.readFully(local);
                if (readInt(local, 0) != LOCAL_SIGNATURE) {
                    throw new ZipException("invalid local header");
                }
                int nameLength = readShort(local, 26);
                int extraLength = readShort(local, 28);
                input.seek(record.localOffset + 30L +
                        nameLength + extraLength);
                if (entry.compressedSize > Integer.MAX_VALUE) {
                    throw new ZipException("entry too large");
                }
                compressed = new byte[(int) entry.compressedSize];
                input.readFully(compressed);
            } finally {
                input.close();
            }
            if (entry.method == ZipEntry.STORED) {
                data = compressed;
                return;
            }
            if (entry.method != ZipEntry.DEFLATED) {
                throw new ZipException("unsupported compression method");
            }
            Inflater inflater = new Inflater(true);
            ByteArrayOutputStream output = new ByteArrayOutputStream(
                    entry.size > Integer.MAX_VALUE
                            ? 0
                            : (int) Math.max(0L, entry.size));
            byte[] buffer = new byte[512];
            try {
                inflater.setInput(compressed);
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
            } catch (DataFormatException error) {
                throw new ZipException(
                        error.getMessage() == null
                                ? "invalid compressed entry"
                                : error.getMessage());
            } finally {
                inflater.end();
            }
            data = output.toByteArray();
        }

        @Override
        public int read() throws IOException {
            load();
            return position >= data.length ? -1 : data[position++] & 0xff;
        }

        @Override
        public int read(byte[] bytes, int offset, int length)
                throws IOException {
            load();
            if (bytes == null) throw new NullPointerException();
            if (offset < 0 || length < 0 ||
                    offset > bytes.length - length) {
                throw new IndexOutOfBoundsException();
            }
            if (position >= data.length) return -1;
            int count = Math.min(length, data.length - position);
            for (int index = 0; index < count; index++) {
                bytes[offset + index] = data[position + index];
            }
            position += count;
            return count;
        }

        @Override
        public int available() throws IOException {
            load();
            return data.length - position;
        }

        @Override
        public long skip(long count) throws IOException {
            load();
            if (count <= 0) return 0;
            int skipped = (int) Math.min(
                    count, data.length - position);
            position += skipped;
            return skipped;
        }

        @Override
        public void close() {
            data = null;
        }
    }

    private static int readShort(byte[] bytes, int offset) {
        return (bytes[offset] & 0xff) |
                (bytes[offset + 1] & 0xff) << 8;
    }

    private static int readInt(byte[] bytes, int offset) {
        return readShort(bytes, offset) |
                readShort(bytes, offset + 2) << 16;
    }

    private static long readUnsignedInt(byte[] bytes, int offset) {
        return readInt(bytes, offset) & 0xffffffffL;
    }
}
