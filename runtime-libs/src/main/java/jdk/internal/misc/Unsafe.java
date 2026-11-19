package jdk.internal.misc;

// jdk.internal.misc.Unsafe：JBR 25 删除/改名了 OpenJDK 11 的对象槽访问面
//（getObject/putObject/compareAndSetObject/defineAnonymousClass 等）。
// 本类在 docs 14 章 §31.4 排除清单（VM 内建独占）：参与编译（source
// override）但不入 runtime.dex，运行期由 VM 内建承载。
public final class Unsafe {
    private static final Unsafe THE_ONE = new Unsafe();

    public static final long INVALID_FIELD_OFFSET = -1L;
    // 常量与 VM intrinsic 同值（core/builtins.cc UnsafeArrayBaseOffset/
    // UnsafeArrayIndexScale）：数组元素区 = data() = field_data()（ObjHeader
    // 尾填充被 length 复用，sizeof(VArray)==sizeof(ObjHeader)）；
    // indexScale = JVM 逻辑元素宽度（B/Z=1、C/S=2、I/F=4、J/D=8）。VM 物理
    // 存储为 8 字节元素槽，Unsafe typed 访问器按 packed 逻辑偏移换算。
    public static final int ARRAY_BOOLEAN_BASE_OFFSET = 0;
    public static final int ARRAY_BYTE_BASE_OFFSET = 0;
    public static final int ARRAY_SHORT_BASE_OFFSET = 0;
    public static final int ARRAY_CHAR_BASE_OFFSET = 0;
    public static final int ARRAY_INT_BASE_OFFSET = 0;
    public static final int ARRAY_LONG_BASE_OFFSET = 0;
    public static final int ARRAY_FLOAT_BASE_OFFSET = 0;
    public static final int ARRAY_DOUBLE_BASE_OFFSET = 0;
    public static final int ARRAY_BOOLEAN_INDEX_SCALE = 1;
    public static final int ARRAY_BYTE_INDEX_SCALE = 1;
    public static final int ARRAY_SHORT_INDEX_SCALE = 2;
    public static final int ARRAY_CHAR_INDEX_SCALE = 2;
    public static final int ARRAY_INT_INDEX_SCALE = 4;
    public static final int ARRAY_LONG_INDEX_SCALE = 8;
    public static final int ARRAY_FLOAT_INDEX_SCALE = 4;
    public static final int ARRAY_DOUBLE_INDEX_SCALE = 8;
    public static final int ADDRESS_SIZE = 8;

    private Unsafe() {
    }

    public static Unsafe getUnsafe() {
        return THE_ONE;
    }

    private static void uoe() {
        throw new UnsupportedOperationException("compile stub");
    }

    // ---- JDK 11 对象槽访问面（JBR 25 已删/改名）----
    public Object getObject(Object o, long offset) { uoe(); return null; }
    public void putObject(Object o, long offset, Object x) { uoe(); }
    public Object getObjectVolatile(Object o, long offset) { uoe(); return null; }
    public void putObjectVolatile(Object o, long offset, Object x) { uoe(); }
    public void putObjectRelease(Object o, long offset, Object x) { uoe(); }
    public void putOrderedObject(Object o, long offset, Object x) { uoe(); }
    public boolean compareAndSetObject(Object o, long offset, Object expected, Object x) {
        uoe(); return false;
    }
    public Object getAndSetObject(Object o, long offset, Object x) { uoe(); return null; }
    public Class<?> defineAnonymousClass(Class<?> hostClass, byte[] data, Object[] cpPatches) {
        uoe(); return null;
    }
    public Class<?> defineClass(String name, byte[] b, int off, int len,
                                ClassLoader loader, java.security.ProtectionDomain pd) {
        uoe(); return null;
    }

    // ---- 绕构造器分配（A 档 Unsafe native 面；sun.misc.Unsafe 门面转发目标）----
    public Object allocateInstance(Class<?> c) { uoe(); return null; }

    // ---- 字段/数组元数据面 ----
    public long objectFieldOffset(java.lang.reflect.Field f) { uoe(); return 0L; }
    public long objectFieldOffset(Class<?> cl, String name) { uoe(); return 0L; }
    public long staticFieldOffset(java.lang.reflect.Field f) { uoe(); return 0L; }
    public long staticFieldOffset(Class<?> cl, String name) { uoe(); return 0L; }
    public Object staticFieldBase(java.lang.reflect.Field f) { uoe(); return null; }
    public int arrayBaseOffset(Class<?> c) { uoe(); return 0; }
    public int arrayIndexScale(Class<?> c) { uoe(); return 0; }
    public void ensureClassInitialized(Class<?> c) { uoe(); }
    public boolean shouldBeInitialized(Class<?> c) { uoe(); return false; }

    // ---- 基本类型槽访问面 ----
    public int getInt(Object o, long offset) { uoe(); return 0; }
    public int getInt(long address) { uoe(); return 0; }
    public void putInt(Object o, long offset, int x) { uoe(); }
    public void putInt(long address, int x) { uoe(); }
    public long getLong(Object o, long offset) { uoe(); return 0L; }
    public long getLong(long address) { uoe(); return 0L; }
    public void putLong(Object o, long offset, long x) { uoe(); }
    public long getLongVolatile(Object o, long offset) { uoe(); return 0L; }
    public void putLong(long address, long x) { uoe(); }
    public byte getByte(Object o, long offset) { uoe(); return 0; }
    public byte getByte(long address) { uoe(); return 0; }
    public byte getByte(java.nio.ByteBuffer buffer, long offset) { uoe(); return 0; }
    public boolean getBoolean(Object o, long offset) { uoe(); return false; }
    public char getChar(Object o, long offset) { uoe(); return 0; }
    public char getChar(long address) { uoe(); return 0; }
    public short getShort(Object o, long offset) { uoe(); return 0; }
    public short getShort(long address) { uoe(); return 0; }
    public void putShort(long address, short x) { uoe(); }
    public float getFloat(Object o, long offset) { uoe(); return 0f; }
    public float getFloat(long address) { uoe(); return 0f; }
    public void putFloat(long address, float x) { uoe(); }
    public double getDouble(Object o, long offset) { uoe(); return 0d; }
    public double getDouble(long address) { uoe(); return 0d; }
    public void putDouble(long address, double x) { uoe(); }
    public void putShort(Object o, long offset, short x) { uoe(); }
    public void putFloat(Object o, long offset, float x) { uoe(); }
    public void putDouble(Object o, long offset, double x) { uoe(); }
    public void putByte(Object o, long offset, byte x) { uoe(); }
    public void putByte(long address, byte x) { uoe(); }
    public void putBoolean(Object o, long offset, boolean x) { uoe(); }
    public void putChar(Object o, long offset, char x) { uoe(); }
    public void putChar(long address, char x) { uoe(); }
    // ---- 原生内存面（sun.misc.Unsafe address 形 / off-heap 池转发目标）----
    public long allocateMemory(long bytes) { uoe(); return 0L; }
    public long reallocateMemory(long address, long bytes) { uoe(); return 0L; }
    public void freeMemory(long address) { uoe(); }
    public void setMemory(long address, long bytes, byte value) { uoe(); }
    public void setMemory(Object o, long offset, long bytes, byte value) { uoe(); }
    public void copyMemory(long srcAddress, long destAddress, long bytes) { uoe(); }
    public void copyMemory(Object srcBase, long srcOffset, Object destBase, long destOffset,
                           long bytes) { uoe(); }
    public void putIntRelease(Object o, long offset, int x) { uoe(); }
    public void putOrderedInt(Object o, long offset, int x) { uoe(); }
    public void putOrderedLong(Object o, long offset, long x) { uoe(); }
    public void putLongRelease(Object o, long offset, long x) { uoe(); }
    public void putLongVolatile(Object o, long offset, long x) { uoe(); }
    // typed Volatile 镜像（sun.misc.Unsafe 门面转发目标；单线程 VM 与 plain 同型）
    public boolean getBooleanVolatile(Object o, long offset) { uoe(); return false; }
    public void putBooleanVolatile(Object o, long offset, boolean x) { uoe(); }
    public byte getByteVolatile(Object o, long offset) { uoe(); return 0; }
    public void putByteVolatile(Object o, long offset, byte x) { uoe(); }
    public short getShortVolatile(Object o, long offset) { uoe(); return 0; }
    public void putShortVolatile(Object o, long offset, short x) { uoe(); }
    public char getCharVolatile(Object o, long offset) { uoe(); return 0; }
    public void putCharVolatile(Object o, long offset, char x) { uoe(); }
    public int getIntVolatile(Object o, long offset) { uoe(); return 0; }
    public void putIntVolatile(Object o, long offset, int x) { uoe(); }
    public float getFloatVolatile(Object o, long offset) { uoe(); return 0f; }
    public void putFloatVolatile(Object o, long offset, float x) { uoe(); }
    public double getDoubleVolatile(Object o, long offset) { uoe(); return 0d; }
    public void putDoubleVolatile(Object o, long offset, double x) { uoe(); }
    public void putIntOpaque(Object o, long offset, int x) { uoe(); }
    public void putLongOpaque(Object o, long offset, long x) { uoe(); }
    public int getAndAddInt(Object o, long offset, int delta) { uoe(); return 0; }
    public long getAndAddLong(Object o, long offset, long delta) { uoe(); return 0L; }
    public int getAndSetInt(Object o, long offset, int newValue) { uoe(); return 0; }
    public long getAndSetLong(Object o, long offset, long newValue) { uoe(); return 0L; }
    public int getIntOpaque(Object o, long offset) { uoe(); return 0; }
    public long getLongOpaque(Object o, long offset) { uoe(); return 0L; }
    public int getIntAcquire(Object o, long offset) { uoe(); return 0; }
    public long getLongAcquire(Object o, long offset) { uoe(); return 0L; }

    // ---- CAS 面 ----
    public boolean compareAndSetInt(Object o, long offset, int expected, int x) {
        uoe(); return false;
    }
    public boolean compareAndSetLong(Object o, long offset, long expected, long x) {
        uoe(); return false;
    }
    public int compareAndExchangeInt(Object o, long offset, int expected, int x) {
        uoe(); return 0;
    }
    public int compareAndExchangeIntAcquire(Object o, long offset, int expected, int x) {
        uoe(); return 0;
    }
    public int compareAndExchangeIntRelease(Object o, long offset, int expected, int x) {
        uoe(); return 0;
    }
    public long compareAndExchangeLong(Object o, long offset, long expected, long x) {
        uoe(); return 0L;
    }
    public long compareAndExchangeLongAcquire(Object o, long offset, long expected, long x) {
        uoe(); return 0L;
    }
    public long compareAndExchangeLongRelease(Object o, long offset, long expected, long x) {
        uoe(); return 0L;
    }
    public boolean weakCompareAndSetInt(Object o, long offset, int expected, int x) {
        uoe(); return false;
    }
    public boolean weakCompareAndSetIntPlain(Object o, long offset, int expected, int x) {
        uoe(); return false;
    }
    public boolean weakCompareAndSetIntAcquire(Object o, long offset, int expected, int x) {
        uoe(); return false;
    }
    public boolean weakCompareAndSetIntRelease(Object o, long offset, int expected, int x) {
        uoe(); return false;
    }
    public boolean weakCompareAndSetLong(Object o, long offset, long expected, long x) {
        uoe(); return false;
    }
    public boolean weakCompareAndSetLongPlain(Object o, long offset, long expected, long x) {
        uoe(); return false;
    }
    public boolean weakCompareAndSetLongAcquire(Object o, long offset, long expected, long x) {
        uoe(); return false;
    }
    public boolean weakCompareAndSetLongRelease(Object o, long offset, long expected, long x) {
        uoe(); return false;
    }

    // ---- 线程/栅栏面 ----
    public void park(boolean isAbsolute, long time) { uoe(); }
    public void unpark(Thread thread) { uoe(); }
    public void fullFence() { uoe(); }
    public void loadFence() { uoe(); }
    public void storeFence() { uoe(); }
    public void storeStoreFence() { uoe(); }
    public void throwException(Throwable t) {
        uoe();
    }

    public int pageSize() { uoe(); return 4096; }
    public int addressSize() { uoe(); return 8; }
    public boolean isBigEndian() { uoe(); return false; }
    public boolean unalignedAccess() { uoe(); return false; }
    public int unalignedAccess0() { uoe(); return 0; }
}
