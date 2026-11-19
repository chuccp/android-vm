package java.util.zip;

public class ZipEntry implements Cloneable {
    public static final int STORED = 0;
    public static final int DEFLATED = 8;

    String name;
    long time = -1L;
    long crc = -1L;
    long size = -1L;
    long compressedSize = -1L;
    int method = -1;
    int flag;
    byte[] extra;
    String comment;

    public ZipEntry(String name) {
        if (name == null) throw new NullPointerException("name");
        if (name.length() > 0xffff) {
            throw new IllegalArgumentException("entry name too long");
        }
        this.name = name;
    }

    public ZipEntry(ZipEntry entry) {
        if (entry == null) throw new NullPointerException("entry");
        name = entry.name;
        time = entry.time;
        crc = entry.crc;
        size = entry.size;
        compressedSize = entry.compressedSize;
        method = entry.method;
        flag = entry.flag;
        extra = entry.extra == null ? null : entry.extra.clone();
        comment = entry.comment;
    }

    public String getName() {
        return name;
    }

    public void setTime(long time) {
        this.time = time;
    }

    public long getTime() {
        return time;
    }

    public void setSize(long size) {
        if (size < 0) throw new IllegalArgumentException("invalid entry size");
        this.size = size;
    }

    public long getSize() {
        return size;
    }

    public void setCompressedSize(long size) {
        this.compressedSize = size;
    }

    public long getCompressedSize() {
        return compressedSize;
    }

    public void setCrc(long crc) {
        if (crc < 0L || crc > 0xffffffffL) {
            throw new IllegalArgumentException("invalid entry crc-32");
        }
        this.crc = crc;
    }

    public long getCrc() {
        return crc;
    }

    public void setMethod(int method) {
        if (method != STORED && method != DEFLATED) {
            throw new IllegalArgumentException("invalid compression method");
        }
        this.method = method;
    }

    public int getMethod() {
        return method;
    }

    public void setExtra(byte[] extra) {
        this.extra = extra == null ? null : extra.clone();
    }


    public void setComment(String comment) {
        if (comment != null && comment.length() > 0xffff) {
            throw new IllegalArgumentException("entry comment too long");
        }
        this.comment = comment;
    }

    public String getComment() {
        return comment;
    }

    public boolean isDirectory() {
        return name.endsWith("/");
    }

    @Override
    public Object clone() {
        return new ZipEntry(this);
    }

    @Override
    public int hashCode() {
        return name.hashCode();
    }

    @Override
    public String toString() {
        return name;
    }

    // ---- overlay：getDataError/getCodeSigners 等 JarEntry 面 API 补全 ----
    static final long DOSTIME_BEFORE_1980 = (30 << 25) | (0 << 21) | (1 << 16);

    transient Throwable xdostime;


    public Object getDataError() {
        return null;
    }

    public java.security.CodeSigner[] getCodeSigners() {
        return null;
    }

    public void setCodeSigners(java.security.CodeSigner[] signers) {
    }

    long getLocalHeaderOffset() {
        return 0L;
    }

}
