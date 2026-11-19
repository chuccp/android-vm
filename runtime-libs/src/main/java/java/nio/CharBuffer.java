package java.nio;

import java.io.IOException;

// 生成补源（sun-compat）：java.nio.CharBuffer（OpenJDK 布局：char[] hb + int offset，
// Buffer 基类提供 mark/position/limit/capacity/address）。
// 需求来源：sun.nio.cs.UTF_8$Decoder.decode(ByteBuffer) 返回 CharBuffer——此前
// runtime.dex 无此类，zip 解压/Charset.decode 链在类链接期 NoClassDefFoundError
// （OneRN Moss 资源 unzip 失败的根因）。堆操作面（wrap/allocate/get/put）按 hb
// 实现；视图/只读面与 JDK 同形，直接缓冲面为 UOE 桩（与 ByteBuffer 口径一致）。
public abstract class CharBuffer extends Buffer
        implements Comparable<CharBuffer>, Appendable, CharSequence, Readable {

    final char[] hb;
    final int offset;
    boolean isReadOnly;

    CharBuffer(int mark, int pos, int lim, int cap) {
        this(mark, pos, lim, cap, null, 0);
    }

    CharBuffer(int mark, int pos, int lim, int cap, char[] hb, int offset) {
        super(mark, pos, lim, cap);
        this.hb = hb;
        this.offset = offset;
    }

    public static CharBuffer allocate(int capacity) {
        if (capacity < 0)
            throw new IllegalArgumentException();
        return new HeapCharBuffer(capacity, capacity);
    }

    public static CharBuffer wrap(char[] array) {
        return wrap(array, 0, array.length);
    }

    public static CharBuffer wrap(char[] array, int offset, int length) {
        try {
            return new HeapCharBuffer(array, offset, length);
        } catch (IllegalArgumentException x) {
            throw new IndexOutOfBoundsException();
        }
    }

    public static CharBuffer wrap(CharSequence csq) {
        return wrap(csq, 0, csq.length());
    }

    public static CharBuffer wrap(CharSequence csq, int start, int end) {
        if (csq == null)
            throw new NullPointerException();
        if (start < 0 || end > csq.length() || start > end)
            throw new IndexOutOfBoundsException();
        int n = end - start;
        char[] chars = new char[n];
        for (int i = 0; i < n; i++)
            chars[i] = csq.charAt(start + i);
        return wrap(chars);
    }

    public abstract CharBuffer slice();

    public CharBuffer slice(int index, int length) {
        throw new UnsupportedOperationException("slice(index,length)");
    }

    public abstract CharBuffer duplicate();

    public abstract CharBuffer asReadOnlyBuffer();

    public abstract char get();

    public abstract char get(int index);

    abstract char getUnchecked(int index);

    public abstract CharBuffer put(char c);

    public abstract CharBuffer put(int index, char c);

    public CharBuffer get(char[] dst, int offset, int length) {
        checkBounds(offset, length, dst.length);
        if (length > remaining())
            throw new BufferUnderflowException();
        int end = offset + length;
        for (int i = offset; i < end; i++)
            dst[i] = get();
        return this;
    }

    public CharBuffer get(char[] dst) {
        return get(dst, 0, dst.length);
    }

    public CharBuffer put(CharBuffer src) {
        if (src == this)
            throw new IllegalArgumentException();
        int n = src.remaining();
        if (n > remaining())
            throw new BufferOverflowException();
        for (int i = 0; i < n; i++)
            put(src.get());
        return this;
    }

    public CharBuffer put(char[] src, int offset, int length) {
        checkBounds(offset, length, src.length);
        if (length > remaining())
            throw new BufferOverflowException();
        int end = offset + length;
        for (int i = offset; i < end; i++)
            put(src[i]);
        return this;
    }

    public CharBuffer put(char[] src) {
        return put(src, 0, src.length);
    }

    public final boolean hasArray() {
        return (hb != null) && !isReadOnly;
    }

    public final char[] array() {
        if (hb == null)
            throw new UnsupportedOperationException();
        if (isReadOnly)
            throw new ReadOnlyBufferException();
        return hb;
    }

    public final int arrayOffset() {
        if (hb == null)
            throw new UnsupportedOperationException();
        return offset;
    }

    public abstract CharBuffer compact();

    public abstract boolean isDirect();

    @Override
    Object base() {
        return hb;
    }

    // 堆字符缓冲无字节序面（BufferMismatch 走逐字符比较路径）。
    ByteOrder charRegionOrder() {
        return null;
    }

    public ByteOrder order() {
        return ByteOrder.BIG_ENDIAN;
    }

    // ---- CharSequence ----

    public final int length() {
        return remaining();
    }

    public final char charAt(int index) {
        int pos = position();
        if (index < 0 || index >= limit() - pos)
            throw new IndexOutOfBoundsException();
        return get(pos + index);
    }

    public CharBuffer subSequence(int start, int end) {
        int pos = position();
        int lim = limit();
        int len = lim - pos;
        if (start < 0 || end > len || start > end)
            throw new IndexOutOfBoundsException();
        CharBuffer b = duplicate();
        b.position(pos + start);
        b.limit(pos + end);
        b.discardMark();
        return b;
    }

    // ---- Appendable ----

    public CharBuffer append(CharSequence csq) {
        if (csq == null)
            csq = "null";
        return append(csq, 0, csq.length());
    }

    public CharBuffer append(CharSequence csq, int start, int end) {
        if (csq == null)
            csq = "null";
        if (start < 0 || end > csq.length() || start > end)
            throw new IndexOutOfBoundsException();
        int len = end - start;
        if (len > remaining())
            throw new BufferOverflowException();
        for (int i = start; i < end; i++)
            put(csq.charAt(i));
        return this;
    }

    public CharBuffer append(char c) {
        put(c);
        return this;
    }

    // ---- Readable ----

    public int read(CharBuffer target) throws IOException {
        int n = Math.min(remaining(), target.remaining());
        for (int i = 0; i < n; i++)
            target.put(get());
        return n;
    }

    @Override
    public String toString() {
        return toString(position(), limit());
    }

    String toString(int start, int end) {
        return new String(hb, offset + start, end - start);
    }

    @Override
    public int hashCode() {
        int h = 1;
        int p = position();
        for (int i = limit() - 1; i >= p; i--)
            h = 31 * h + (int) get(i);
        return h;
    }

    @Override
    public boolean equals(Object ob) {
        if (this == ob)
            return true;
        if (!(ob instanceof CharBuffer))
            return false;
        CharBuffer that = (CharBuffer) ob;
        if (this.remaining() != that.remaining())
            return false;
        int p = this.position();
        int q = that.position();
        for (int i = this.limit() - 1, j = that.limit() - 1; i >= p; i--, j--) {
            char v1 = this.get(i);
            char v2 = that.get(j);
            if (v1 != v2)
                return false;
        }
        return true;
    }

    public int compareTo(CharBuffer that) {
        int n = this.position() + Math.min(this.remaining(), that.remaining());
        for (int i = this.position(), j = that.position(); i < n; i++, j++) {
            char v1 = this.get(i);
            char v2 = that.get(j);
            if (v1 == v2)
                continue;
            return (v1 < v2) ? -1 : 1;
        }
        return this.remaining() - that.remaining();
    }

    // 堆字符 buffer 最小实现（wrap/allocate 落点；hb 全量 + 窗口语义）
    private static final class HeapCharBuffer extends CharBuffer {
        HeapCharBuffer(char[] buf, int off, int len) {
            super(-1, off, off + len, buf.length, buf, 0);
        }

        HeapCharBuffer(int cap, int lim) {
            super(-1, 0, lim, cap, new char[cap], 0);
        }

        @Override
        public char get() {
            return hb[offset + nextGetIndex()];
        }

        @Override
        public char get(int i) {
            return hb[offset + checkIndex(i)];
        }

        @Override
        char getUnchecked(int i) {
            return hb[offset + i];
        }

        @Override
        public CharBuffer put(char c) {
            hb[offset + nextPutIndex()] = c;
            return this;
        }

        @Override
        public CharBuffer put(int i, char c) {
            hb[offset + checkIndex(i)] = c;
            return this;
        }

        @Override
        public CharBuffer slice() {
            return new HeapCharBuffer(hb, offset + position(), remaining());
        }

        @Override
        public CharBuffer slice(int index, int length) {
            checkIndex(index, length);
            return new HeapCharBuffer(hb, offset + index, length);
        }

        @Override
        public CharBuffer duplicate() {
            HeapCharBuffer b = new HeapCharBuffer(hb, offset, capacity());
            b.limit(limit());
            b.position(position());
            return b;
        }

        @Override
        public CharBuffer asReadOnlyBuffer() {
            HeapCharBuffer b = new HeapCharBuffer(hb, offset, capacity());
            b.limit(limit());
            b.position(position());
            b.isReadOnly = true;
            return b;
        }

        @Override
        public boolean isReadOnly() {
            return isReadOnly;
        }

        @Override
        public CharBuffer compact() {
            int rem = remaining();
            if (rem > 0)
                System.arraycopy(hb, offset + position(), hb, offset, rem);
            position(rem);
            limit(capacity());
            return this;
        }

        @Override
        public boolean isDirect() {
            return false;
        }
    }
}
