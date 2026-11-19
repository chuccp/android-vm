package java.util.zip;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;

public class ZipOutputStream extends DeflaterOutputStream {
    public static final int STORED = ZipEntry.STORED;
    public static final int DEFLATED = ZipEntry.DEFLATED;

    private static final int UTF8_FLAG = 0x0800;

    private static final class Record {
        ZipEntry entry;
        byte[] name;
        long offset;
    }

    private ByteArrayOutputStream current;
    private ZipEntry currentEntry;
    private Record[] records = new Record[8];
    private int recordCount;
    private long written;
    private int defaultMethod = DEFLATED;
    private byte[] archiveComment;
    private boolean finished;

    public ZipOutputStream(OutputStream output) {
        super(output);
    }

    public void setMethod(int method) {
        if (method != STORED && method != DEFLATED) {
            throw new IllegalArgumentException("invalid compression method");
        }
        defaultMethod = method;
    }

    public void setLevel(int level) {
        def.setLevel(level);
    }

    public void setComment(String comment) {
        archiveComment = comment == null
                ? null
                : comment.getBytes();
        if (archiveComment != null && archiveComment.length > 0xffff) {
            throw new IllegalArgumentException("ZIP file comment too long");
        }
    }

    public void putNextEntry(ZipEntry entry) throws IOException {
        if (entry == null) throw new NullPointerException("entry");
        closeEntry();
        byte[] name = entry.getName().getBytes();
        for (int index = 0; index < recordCount; index++) {
            byte[] existing = records[index].name;
            if (sameBytes(existing, name)) {
                throw new ZipException("duplicate entry: " + entry.getName());
            }
        }
        currentEntry = new ZipEntry(entry);
        if (currentEntry.method < 0) currentEntry.method = defaultMethod;
        current = new ByteArrayOutputStream();
    }

    @Override
    public void write(int value) throws IOException {
        requireEntry();
        current.write(value);
    }

    @Override
    public void write(byte[] bytes, int offset, int length)
            throws IOException {
        requireEntry();
        current.write(bytes, offset, length);
    }

    private void requireEntry() throws IOException {
        if (current == null) {
            throw new ZipException("no current ZIP entry");
        }
    }

    public void closeEntry() throws IOException {
        if (current == null) return;
        byte[] raw = current.toByteArray();
        CRC32 crc = new CRC32();
        crc.update(raw);
        byte[] data = raw;
        if (currentEntry.method == DEFLATED) {
            data = deflate(raw);
        } else if (currentEntry.method != STORED) {
            throw new ZipException("unsupported compression method");
        }

        currentEntry.crc = crc.getValue();
        currentEntry.size = raw.length;
        currentEntry.compressedSize = data.length;
        byte[] name = currentEntry.name.getBytes();
        byte[] extra = currentEntry.extra == null
                ? new byte[0]
                : currentEntry.extra;

        Record record = new Record();
        record.entry = currentEntry;
        record.name = name;
        record.offset = written;
        append(record);

        writeInt(0x04034b50);
        writeShort(currentEntry.method == DEFLATED ? 20 : 10);
        writeShort(UTF8_FLAG);
        writeShort(currentEntry.method);
        writeShort(0);
        writeShort(0);
        writeInt((int) currentEntry.crc);
        writeInt(data.length);
        writeInt(raw.length);
        writeShort(name.length);
        writeShort(extra.length);
        destination.write(name, 0, name.length);
        destination.write(extra, 0, extra.length);
        destination.write(data, 0, data.length);
        written += 30L + name.length + extra.length + data.length;

        current = null;
        currentEntry = null;
    }

    private void append(Record record) {
        if (recordCount == records.length) {
            Record[] grown = new Record[records.length * 2];
            System.arraycopy(records, 0, grown, 0, records.length);
            records = grown;
        }
        records[recordCount++] = record;
    }

    private byte[] deflate(byte[] raw) throws IOException {
        Deflater deflater = new Deflater(
                Deflater.DEFAULT_COMPRESSION, true);
        ByteArrayOutputStream compressed = new ByteArrayOutputStream();
        byte[] buffer = new byte[512];
        try {
            deflater.setInput(raw);
            deflater.finish();
            while (!deflater.finished()) {
                int count = deflater.deflate(buffer);
                compressed.write(buffer, 0, count);
            }
        } finally {
            deflater.end();
        }
        return compressed.toByteArray();
    }

    public void finish() throws IOException {
        if (finished) return;
        closeEntry();
        long centralOffset = written;
        for (int index = 0; index < recordCount; index++) {
            Record record = records[index];
            ZipEntry entry = record.entry;
            byte[] extra = entry.extra == null
                    ? new byte[0]
                    : entry.extra;
            byte[] comment = entry.comment == null
                    ? new byte[0]
                    : entry.comment.getBytes();
            writeInt(0x02014b50);
            writeShort(20);
            writeShort(entry.method == DEFLATED ? 20 : 10);
            writeShort(UTF8_FLAG);
            writeShort(entry.method);
            writeShort(0);
            writeShort(0);
            writeInt((int) entry.crc);
            writeInt((int) entry.compressedSize);
            writeInt((int) entry.size);
            writeShort(record.name.length);
            writeShort(extra.length);
            writeShort(comment.length);
            writeShort(0);
            writeShort(0);
            writeInt(entry.isDirectory() ? 0x10 : 0);
            writeInt((int) record.offset);
            destination.write(record.name, 0, record.name.length);
            destination.write(extra, 0, extra.length);
            destination.write(comment, 0, comment.length);
            written += 46L + record.name.length +
                    extra.length + comment.length;
        }
        long centralSize = written - centralOffset;
        writeInt(0x06054b50);
        writeShort(0);
        writeShort(0);
        writeShort(recordCount);
        writeShort(recordCount);
        writeInt((int) centralSize);
        writeInt((int) centralOffset);
        int commentLength = archiveComment == null
                ? 0
                : archiveComment.length;
        writeShort(commentLength);
        if (commentLength > 0) {
            destination.write(archiveComment, 0, commentLength);
        }
        destination.flush();
        finished = true;
    }

    @Override
    public void flush() throws IOException {
        destination.flush();
    }

    @Override
    public void close() throws IOException {
        if (finished) return;
        try {
            finish();
        } finally {
            destination.close();
        }
    }

    private void writeShort(int value) throws IOException {
        destination.write(value);
        destination.write(value >>> 8);
    }

    private void writeInt(int value) throws IOException {
        writeShort(value);
        writeShort(value >>> 16);
    }

    private static boolean sameBytes(byte[] left, byte[] right) {
        if (left.length != right.length) return false;
        for (int index = 0; index < left.length; index++) {
            if (left[index] != right[index]) return false;
        }
        return true;
    }
}
