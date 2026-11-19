package java.nio;

// 生成补源（sun-compat）：java.nio.ByteBuffer（OpenJDK 布局：byte[] hb + int offset
// （Buffer：mark/position/limit/capacity/address）；implements Comparable<ByteBuffer>）。
// P0 翻转：内建空骨架已删——解除对 lib NIO（Buffer/MappedByteBuffer/DirectByteBuffer
// 链）的遮蔽；InMemoryDexClassLoader buffer 读取接缝按 hb/offset/position 字段取
// 窗口（core/builtins.cc ExtractDexBytes）。堆操作面（wrap/get/put）按 hb 实现；
// 视图/字节序面为 UOE 桩（Direct/MappedByteBuffer 子类各自实现）。
public abstract class ByteBuffer extends Buffer implements Comparable<ByteBuffer> {

    /** 直接缓冲区（VM intrinsic：mmap 匿名区 + DirectByteBuffer 数据面）。 */
    public static native ByteBuffer allocateDirect(int capacity);

    final byte[] hb;
    final int offset;
    boolean isReadOnly;

    ByteBuffer(int mark, int pos, int lim, int cap, byte[] hb, int offset, Object ob) {
        super(mark, pos, lim, cap);
        this.hb = hb;
        this.offset = offset;
    }

    public static ByteBuffer allocate(int capacity) {
        if (capacity < 0)
            throw new IllegalArgumentException();
        return new HeapByteBuffer(capacity, capacity);
    }

    public static ByteBuffer wrap(byte[] array) {
        return wrap(array, 0, array.length);
    }

    public static ByteBuffer wrap(byte[] array, int offset, int length) {
        try {
            return new HeapByteBuffer(array, offset, length);
        } catch (IllegalArgumentException x) {
            throw new IndexOutOfBoundsException();
        }
    }

    public abstract ByteBuffer slice();

    public ByteBuffer slice(int index, int length) {
        throw new UnsupportedOperationException("slice(index,length)");
    }

    public abstract ByteBuffer duplicate();

    public abstract ByteBuffer asReadOnlyBuffer();

    public abstract byte get();

    public abstract byte get(int index);

    public abstract ByteBuffer put(byte b);

    public abstract ByteBuffer put(int index, byte b);

    public ByteBuffer get(byte[] dst, int offset, int length) {
        checkBounds(offset, length, dst.length);
        if (length > remaining())
            throw new BufferUnderflowException();
        int end = offset + length;
        for (int i = offset; i < end; i++)
            dst[i] = get();
        return this;
    }

    public ByteBuffer get(byte[] dst) {
        return get(dst, 0, dst.length);
    }

    public ByteBuffer put(ByteBuffer src) {
        if (src == this)
            throw new IllegalArgumentException();
        int n = src.remaining();
        if (n > remaining())
            throw new BufferOverflowException();
        for (int i = 0; i < n; i++)
            put(src.get());
        return this;
    }

    public ByteBuffer put(byte[] src, int offset, int length) {
        checkBounds(offset, length, src.length);
        if (length > remaining())
            throw new BufferOverflowException();
        int end = offset + length;
        for (int i = offset; i < end; i++)
            put(src[i]);
        return this;
    }

    public ByteBuffer put(byte[] src) {
        return put(src, 0, src.length);
    }

    public final boolean hasArray() {
        return (hb != null) && !isReadOnly;
    }

    public final byte[] array() {
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

    public abstract ByteBuffer compact();

    public abstract boolean isDirect();

    @Override
    public String toString() {
        return getClass().getName() + "[pos=" + position() + " lim=" + limit() + " cap=" + capacity() + "]";
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
        if (!(ob instanceof ByteBuffer))
            return false;
        ByteBuffer that = (ByteBuffer) ob;
        if (this.remaining() != that.remaining())
            return false;
        int p = this.position();
        int q = that.position();
        for (int i = this.limit() - 1, j = that.limit() - 1; i >= p; i--, j--) {
            byte v1 = this.get(i);
            byte v2 = that.get(j);
            if (v1 != v2)
                return false;
        }
        return true;
    }

    public int compareTo(ByteBuffer that) {
        int n = this.position() + Math.min(this.remaining(), that.remaining());
        for (int i = this.position(), j = that.position(); i < n; i++, j++) {
            int v1 = this.get(i);
            int v2 = that.get(j);
            if (v1 == v2)
                continue;
            return (v1 < v2) ? -1 : 1;
        }
        return this.remaining() - that.remaining();
    }

    public final ByteOrder order() {
        return ByteOrder.BIG_ENDIAN;
    }

    public ByteBuffer order(ByteOrder bo) {
        return this;
    }

    @Override
    Object base() {
        return hb;
    }

    public abstract char getChar();

    public abstract char getChar(int index);

    public abstract ByteBuffer putChar(char value);

    public abstract ByteBuffer putChar(int index, char value);

    public abstract CharBuffer asCharBuffer();

    public abstract short getShort();

    public abstract short getShort(int index);

    public abstract ByteBuffer putShort(short value);

    public abstract ByteBuffer putShort(int index, short value);

    public abstract ShortBuffer asShortBuffer();

    public abstract int getInt();

    public abstract int getInt(int index);

    public abstract ByteBuffer putInt(int value);

    public abstract ByteBuffer putInt(int index, int value);

    public abstract IntBuffer asIntBuffer();

    public abstract long getLong();

    public abstract long getLong(int index);

    public abstract ByteBuffer putLong(long value);

    public abstract ByteBuffer putLong(int index, long value);

    public abstract LongBuffer asLongBuffer();

    public abstract float getFloat();

    public abstract float getFloat(int index);

    public abstract ByteBuffer putFloat(float value);

    public abstract ByteBuffer putFloat(int index, float value);

    public abstract FloatBuffer asFloatBuffer();

    public abstract double getDouble();

    public abstract double getDouble(int index);

    public abstract ByteBuffer putDouble(double value);

    public abstract ByteBuffer putDouble(int index, double value);

    public abstract DoubleBuffer asDoubleBuffer();

    // 堆 buffer 最小实现（wrap/allocate 落点；hb 全量 + 窗口语义；BIG_ENDIAN）
    private static final class HeapByteBuffer extends ByteBuffer {
        HeapByteBuffer(byte[] buf, int off, int len) {
            super(-1, off, off + len, buf.length, buf, 0, null);
        }

        HeapByteBuffer(int cap, int lim) {
            super(-1, 0, lim, cap, new byte[cap], 0, null);
        }

        @Override public byte get() {
            return hb[offset + nextGetIndex()];
        }

        @Override public byte get(int i) {
            return hb[offset + checkIndex(i)];
        }

        @Override public ByteBuffer put(byte b) {
            hb[offset + nextPutIndex()] = b;
            return this;
        }

        @Override public ByteBuffer put(int i, byte b) {
            hb[offset + checkIndex(i)] = b;
            return this;
        }

        @Override public ByteBuffer slice() {
            return new HeapByteBuffer(hb, offset + position(), remaining());
        }

        @Override public ByteBuffer slice(int index, int length) {
            checkIndex(index, length);
            return new HeapByteBuffer(hb, offset + index, length);
        }

        @Override public ByteBuffer duplicate() {
            HeapByteBuffer b = new HeapByteBuffer(hb, offset, capacity());
            b.limit(limit());
            b.position(position());
            return b;
        }

        @Override public ByteBuffer asReadOnlyBuffer() {
            HeapByteBuffer b = new HeapByteBuffer(hb, offset, capacity());
            b.limit(limit());
            b.position(position());
            b.isReadOnly = true;
            return b;
        }

        @Override public boolean isDirect() {
            return false;
        }

        @Override public boolean isReadOnly() {
            return isReadOnly;
        }

        @Override public ByteBuffer compact() {
            int rem = remaining();
            if (rem > 0)
                System.arraycopy(hb, offset + position(), hb, offset, rem);
            position(rem);
            limit(capacity());
            return this;
        }

        @Override public char getChar() {
            int i = nextGetIndex(2);
            return (char) (((hb[offset + i] & 0xff) << 8) | (hb[offset + i + 1] & 0xff));
        }

        @Override public char getChar(int i) {
            checkIndex(i, 2);
            return (char) (((hb[offset + i] & 0xff) << 8) | (hb[offset + i + 1] & 0xff));
        }

        @Override public ByteBuffer putChar(char v) {
            int i = nextPutIndex(2);
            hb[offset + i] = (byte) (v >> 8);
            hb[offset + i + 1] = (byte) v;
            return this;
        }

        @Override public ByteBuffer putChar(int i, char v) {
            checkIndex(i, 2);
            hb[offset + i] = (byte) (v >> 8);
            hb[offset + i + 1] = (byte) v;
            return this;
        }

        @Override public CharBuffer asCharBuffer() { throw uoe(); }

        @Override public short getShort() {
            int i = nextGetIndex(2);
            return (short) (((hb[offset + i] & 0xff) << 8) | (hb[offset + i + 1] & 0xff));
        }

        @Override public short getShort(int i) {
            checkIndex(i, 2);
            return (short) (((hb[offset + i] & 0xff) << 8) | (hb[offset + i + 1] & 0xff));
        }

        @Override public ByteBuffer putShort(short v) {
            int i = nextPutIndex(2);
            hb[offset + i] = (byte) (v >> 8);
            hb[offset + i + 1] = (byte) v;
            return this;
        }

        @Override public ByteBuffer putShort(int i, short v) {
            checkIndex(i, 2);
            hb[offset + i] = (byte) (v >> 8);
            hb[offset + i + 1] = (byte) v;
            return this;
        }

        @Override public ShortBuffer asShortBuffer() { throw uoe(); }

        @Override public int getInt() {
            int i = nextGetIndex(4);
            return ((hb[offset + i] & 0xff) << 24) | ((hb[offset + i + 1] & 0xff) << 16)
                | ((hb[offset + i + 2] & 0xff) << 8) | (hb[offset + i + 3] & 0xff);
        }

        @Override public int getInt(int i) {
            checkIndex(i, 4);
            return ((hb[offset + i] & 0xff) << 24) | ((hb[offset + i + 1] & 0xff) << 16)
                | ((hb[offset + i + 2] & 0xff) << 8) | (hb[offset + i + 3] & 0xff);
        }

        @Override public ByteBuffer putInt(int v) {
            int i = nextPutIndex(4);
            hb[offset + i] = (byte) (v >> 24);
            hb[offset + i + 1] = (byte) (v >> 16);
            hb[offset + i + 2] = (byte) (v >> 8);
            hb[offset + i + 3] = (byte) v;
            return this;
        }

        @Override public ByteBuffer putInt(int i, int v) {
            checkIndex(i, 4);
            hb[offset + i] = (byte) (v >> 24);
            hb[offset + i + 1] = (byte) (v >> 16);
            hb[offset + i + 2] = (byte) (v >> 8);
            hb[offset + i + 3] = (byte) v;
            return this;
        }

        @Override public IntBuffer asIntBuffer() { throw uoe(); }

        @Override public long getLong() {
            int i = nextGetIndex(8);
            return getLongAt(i);
        }

        @Override public long getLong(int i) {
            checkIndex(i, 8);
            return getLongAt(i);
        }

        private long getLongAt(int i) {
            return ((long) (hb[offset + i] & 0xff) << 56) | ((long) (hb[offset + i + 1] & 0xff) << 48)
                | ((long) (hb[offset + i + 2] & 0xff) << 40) | ((long) (hb[offset + i + 3] & 0xff) << 32)
                | ((long) (hb[offset + i + 4] & 0xff) << 24) | ((long) (hb[offset + i + 5] & 0xff) << 16)
                | ((long) (hb[offset + i + 6] & 0xff) << 8) | ((long) (hb[offset + i + 7] & 0xff));
        }

        @Override public ByteBuffer putLong(long v) {
            int i = nextPutIndex(8);
            putLongAt(i, v);
            return this;
        }

        @Override public ByteBuffer putLong(int i, long v) {
            checkIndex(i, 8);
            putLongAt(i, v);
            return this;
        }

        private void putLongAt(int i, long v) {
            hb[offset + i] = (byte) (v >> 56);
            hb[offset + i + 1] = (byte) (v >> 48);
            hb[offset + i + 2] = (byte) (v >> 40);
            hb[offset + i + 3] = (byte) (v >> 32);
            hb[offset + i + 4] = (byte) (v >> 24);
            hb[offset + i + 5] = (byte) (v >> 16);
            hb[offset + i + 6] = (byte) (v >> 8);
            hb[offset + i + 7] = (byte) v;
        }

        @Override public LongBuffer asLongBuffer() { throw uoe(); }

        @Override public float getFloat() {
            return Float.intBitsToFloat(getInt());
        }

        @Override public float getFloat(int i) {
            return Float.intBitsToFloat(getInt(i));
        }

        @Override public ByteBuffer putFloat(float v) {
            putInt(Float.floatToRawIntBits(v));
            return this;
        }

        @Override public ByteBuffer putFloat(int i, float v) {
            putInt(i, Float.floatToRawIntBits(v));
            return this;
        }

        @Override public FloatBuffer asFloatBuffer() { throw uoe(); }

        @Override public double getDouble() {
            return Double.longBitsToDouble(getLong());
        }

        @Override public double getDouble(int i) {
            return Double.longBitsToDouble(getLong(i));
        }

        @Override public ByteBuffer putDouble(double v) {
            putLong(Double.doubleToRawLongBits(v));
            return this;
        }

        @Override public ByteBuffer putDouble(int i, double v) {
            putLong(i, Double.doubleToRawLongBits(v));
            return this;
        }

        @Override public DoubleBuffer asDoubleBuffer() { throw uoe(); }

        private static UnsupportedOperationException uoe() {
            return new UnsupportedOperationException("view surface");
        }
    }
}
