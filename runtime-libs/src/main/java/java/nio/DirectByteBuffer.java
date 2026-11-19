package java.nio;

// 生成补源（sun-compat）：java.nio.DirectByteBuffer——mmap/direct 缓冲数据面
// （OpenJDK 形态：FileChannel.map 的落点类；core/builtins_io.cc FCNativeMap
// 经构造器 (long,int,Object) 例化，address = 映射地址）。
// 数据面按 Buffer.address 绝对地址字节访问（jdk.internal.misc.Unsafe
// getByte(long)/putByte(long,byte) 地址形缝，core/builtins_unsafe.cc），
// BIG_ENDIAN 字节序（ByteBuffer.order 契约）；bulk get/put 走 ByteBuffer
// 对 get()/put(byte) 的循环面（自动承接）。
class DirectByteBuffer extends MappedByteBuffer {

    DirectByteBuffer(long addr, int cap, Object ob) {
        // 初始态同 JDK：mark=-1 / position=0 / limit=cap / capacity=cap
        //（此前 pos=cap 使新建直接缓冲区不可写：put 即 BufferOverflowException）
        super(-1, 0, cap, cap);
        this.address = addr;
    }

    @Override public boolean isDirect() { return true; }
    @Override public boolean isReadOnly() { return false; }

    // ---- 单字节绝对地址访问（地址形 Unsafe 缝）----
    private byte byteAt(int i) {
        return UNSAFE.getByte(address + i);
    }

    private void setByteAt(int i, byte b) {
        UNSAFE.putByte(address + i, b);
    }

    @Override public byte get() {
        return byteAt(nextGetIndex());
    }

    @Override public byte get(int i) {
        return byteAt(checkIndex(i));
    }

    @Override public ByteBuffer put(byte b) {
        setByteAt(nextPutIndex(), b);
        return this;
    }

    @Override public ByteBuffer put(int i, byte b) {
        setByteAt(checkIndex(i), b);
        return this;
    }

    @Override public ByteBuffer slice() {
        DirectByteBuffer b = new DirectByteBuffer(address + position(), remaining(), null);
        return b;
    }

    @Override public ByteBuffer slice(int index, int length) {
        checkIndex(index, length);
        return new DirectByteBuffer(address + index, length, null);
    }

    @Override public ByteBuffer duplicate() {
        DirectByteBuffer b = new DirectByteBuffer(address, capacity(), null);
        b.limit(limit());
        b.position(position());
        return b;
    }

    @Override public ByteBuffer asReadOnlyBuffer() {
        throw new UnsupportedOperationException("read-only direct view");
    }

    @Override public ByteBuffer compact() {
        int rem = remaining();
        for (int i = 0; i < rem; i++) {
            setByteAt(i, byteAt(position() + i));
        }
        position(rem);
        limit(capacity());
        return this;
    }

    // ---- typed 面（BIG_ENDIAN；字节组合）----

    @Override public char getChar() {
        int i = nextGetIndex(2);
        return (char) (((byteAt(i) & 0xff) << 8) | (byteAt(i + 1) & 0xff));
    }

    @Override public char getChar(int i) {
        checkIndex(i, 2);
        return (char) (((byteAt(i) & 0xff) << 8) | (byteAt(i + 1) & 0xff));
    }

    @Override public ByteBuffer putChar(char v) {
        int i = nextPutIndex(2);
        setByteAt(i, (byte) (v >> 8));
        setByteAt(i + 1, (byte) v);
        return this;
    }

    @Override public ByteBuffer putChar(int i, char v) {
        checkIndex(i, 2);
        setByteAt(i, (byte) (v >> 8));
        setByteAt(i + 1, (byte) v);
        return this;
    }

    @Override public CharBuffer asCharBuffer() { throw uoe(); }

    @Override public short getShort() {
        int i = nextGetIndex(2);
        return (short) (((byteAt(i) & 0xff) << 8) | (byteAt(i + 1) & 0xff));
    }

    @Override public short getShort(int i) {
        checkIndex(i, 2);
        return (short) (((byteAt(i) & 0xff) << 8) | (byteAt(i + 1) & 0xff));
    }

    @Override public ByteBuffer putShort(short v) {
        int i = nextPutIndex(2);
        setByteAt(i, (byte) (v >> 8));
        setByteAt(i + 1, (byte) v);
        return this;
    }

    @Override public ByteBuffer putShort(int i, short v) {
        checkIndex(i, 2);
        setByteAt(i, (byte) (v >> 8));
        setByteAt(i + 1, (byte) v);
        return this;
    }

    @Override public ShortBuffer asShortBuffer() { throw uoe(); }

    @Override public int getInt() {
        return getInt(nextGetIndex(4));
    }

    @Override public int getInt(int i) {
        checkIndex(i, 4);
        return ((byteAt(i) & 0xff) << 24) | ((byteAt(i + 1) & 0xff) << 16)
                | ((byteAt(i + 2) & 0xff) << 8) | (byteAt(i + 3) & 0xff);
    }

    @Override public ByteBuffer putInt(int v) {
        putInt(nextPutIndex(4), v);
        return this;
    }

    @Override public ByteBuffer putInt(int i, int v) {
        checkIndex(i, 4);
        setByteAt(i, (byte) (v >> 24));
        setByteAt(i + 1, (byte) (v >> 16));
        setByteAt(i + 2, (byte) (v >> 8));
        setByteAt(i + 3, (byte) v);
        return this;
    }

    @Override public IntBuffer asIntBuffer() { throw uoe(); }

    @Override public long getLong() {
        return getLong(nextGetIndex(8));
    }

    @Override public long getLong(int i) {
        checkIndex(i, 8);
        return ((long) (byteAt(i) & 0xff) << 56) | ((long) (byteAt(i + 1) & 0xff) << 48)
                | ((long) (byteAt(i + 2) & 0xff) << 40) | ((long) (byteAt(i + 3) & 0xff) << 32)
                | ((long) (byteAt(i + 4) & 0xff) << 24) | ((long) (byteAt(i + 5) & 0xff) << 16)
                | ((long) (byteAt(i + 6) & 0xff) << 8) | (byteAt(i + 7) & 0xff);
    }

    @Override public ByteBuffer putLong(long v) {
        putLong(nextPutIndex(8), v);
        return this;
    }

    @Override public ByteBuffer putLong(int i, long v) {
        checkIndex(i, 8);
        setByteAt(i, (byte) (v >> 56));
        setByteAt(i + 1, (byte) (v >> 48));
        setByteAt(i + 2, (byte) (v >> 40));
        setByteAt(i + 3, (byte) (v >> 32));
        setByteAt(i + 4, (byte) (v >> 24));
        setByteAt(i + 5, (byte) (v >> 16));
        setByteAt(i + 6, (byte) (v >> 8));
        setByteAt(i + 7, (byte) v);
        return this;
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
