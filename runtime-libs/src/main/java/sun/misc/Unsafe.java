package sun.misc;

// sun.misc.Unsafe legacy 门面（C 档 lib 壳，docs 14 章 §31.7「默认 lib」）：
// 知乎考卷 java8/util/k0 <clinit> 链的 legacy 获取面（theUnsafe 反射 /
// getUnsafe()）。本类 = OpenJDK sun.misc.Unsafe 公开面（JDK 8 + JDK 9+ 并集），
// 方法转发 jdk.internal.misc.Unsafe（VM 内建承载，签名差异按各自公开面适配：
// compareAndSwapObject -> compareAndSetObject 等改名族、unpark(Object)/(Thread)
// 双形）。VM 侧只留 A 档原语缝（allocateInstance 绕构造器分配、staticField*
// 静态槽、typed 槽面），语义在 core/builtins.cc。
//
// 获取面口径：getUnsafe() 直返 theUnsafe（本 VM 无 caller-sensitive 模块封装
// 语义（jdk.internal.reflect.Reflection.getCallerClass 恒 null），对齐 Android
// libcore 无检查口径）；theUnsafe 经 getDeclaredField 反射路径可取（私有静态
// 单例，与 OpenJDK 同名同形）。
//
// 偏移寻址契约与 jdk.internal.misc.Unsafe 一致：对象槽 = 字段槽字节偏移；
// 静态槽 = staticFieldOffset 带标记偏移（VM 侧 kStaticSlotTag）；数组 =
// arrayBaseOffset() + i * arrayIndexScale()（本 VM 统一 8 字节元素槽，常量
// 与 VM intrinsic 同值）。
public final class Unsafe {

    // ---- 常量（与 core/builtins.cc UnsafeArrayBaseOffset/UnsafeArrayIndexScale
    //      同值：数组元素区 = data() = field_data() + (sizeof(VArray)-
    //      sizeof(ObjHeader)) = field_data()（ObjHeader 尾填充被 length 复用，
    //      sizeof 相等）；indexScale = JVM 逻辑元素宽度（B/Z=1、C/S=2、
    //      I/F=4、J/D/引用=8）。VM 物理存储仍是 8 字节槽，Unsafe typed
    //      访问器按 packed 逻辑偏移换算（见 core/builtins_unsafe.cc）。）----
    public static final int INVALID_FIELD_OFFSET = -1;
    public static final int ARRAY_BOOLEAN_BASE_OFFSET = 0;
    public static final int ARRAY_BYTE_BASE_OFFSET = 0;
    public static final int ARRAY_SHORT_BASE_OFFSET = 0;
    public static final int ARRAY_CHAR_BASE_OFFSET = 0;
    public static final int ARRAY_INT_BASE_OFFSET = 0;
    public static final int ARRAY_LONG_BASE_OFFSET = 0;
    public static final int ARRAY_FLOAT_BASE_OFFSET = 0;
    public static final int ARRAY_DOUBLE_BASE_OFFSET = 0;
    public static final int ARRAY_OBJECT_BASE_OFFSET = 0;
    public static final int ARRAY_BOOLEAN_INDEX_SCALE = 1;
    public static final int ARRAY_BYTE_INDEX_SCALE = 1;
    public static final int ARRAY_SHORT_INDEX_SCALE = 2;
    public static final int ARRAY_CHAR_INDEX_SCALE = 2;
    public static final int ARRAY_INT_INDEX_SCALE = 4;
    public static final int ARRAY_LONG_INDEX_SCALE = 8;
    public static final int ARRAY_FLOAT_INDEX_SCALE = 4;
    public static final int ARRAY_DOUBLE_INDEX_SCALE = 8;
    public static final int ARRAY_OBJECT_INDEX_SCALE = 8;
    public static final int ADDRESS_SIZE = 8;

    private static final Unsafe theUnsafe = new Unsafe();
    private static final jdk.internal.misc.Unsafe internal =
            jdk.internal.misc.Unsafe.getUnsafe();

    private Unsafe() {
    }

    public static Unsafe getUnsafe() {
        return theUnsafe;
    }

    private static UnsupportedOperationException noCode(String name) {
        return new UnsupportedOperationException("no code: sun.misc.Unsafe." + name);
    }

    // ---- 字段/静态槽元数据面 ----
    public long objectFieldOffset(java.lang.reflect.Field f) {
        return internal.objectFieldOffset(f);
    }

    public long staticFieldOffset(java.lang.reflect.Field f) {
        return internal.staticFieldOffset(f);
    }

    public Object staticFieldBase(java.lang.reflect.Field f) {
        return internal.staticFieldBase(f);
    }

    public boolean shouldBeInitialized(Class<?> c) {
        return internal.shouldBeInitialized(c);
    }

    public void ensureClassInitialized(Class<?> c) {
        internal.ensureClassInitialized(c);
    }

    public int arrayBaseOffset(Class<?> arrayClass) {
        return internal.arrayBaseOffset(arrayClass);
    }

    public int arrayIndexScale(Class<?> arrayClass) {
        return internal.arrayIndexScale(arrayClass);
    }

    public int addressSize() {
        return internal.addressSize();
    }

    public int pageSize() {
        return 4096;
    }

    // ---- 绕构造器分配（A 档 Unsafe native 面；v1 头号刚需）----
    public Object allocateInstance(Class<?> cls) throws InstantiationException {
        return internal.allocateInstance(cls);
    }

    // ---- 对象槽访问面 ----
    public Object getObject(Object o, long offset) {
        return internal.getObject(o, offset);
    }

    public void putObject(Object o, long offset, Object x) {
        internal.putObject(o, offset, x);
    }

    public Object getObjectVolatile(Object o, long offset) {
        return internal.getObjectVolatile(o, offset);
    }

    public void putObjectVolatile(Object o, long offset, Object x) {
        internal.putObjectVolatile(o, offset, x);
    }

    public void putOrderedObject(Object o, long offset, Object x) {
        internal.putOrderedObject(o, offset, x);
    }

    public final boolean compareAndSwapObject(Object o, long offset, Object expected, Object x) {
        return internal.compareAndSetObject(o, offset, expected, x);
    }

    public final Object getAndSetObject(Object o, long offset, Object x) {
        return internal.getAndSetObject(o, offset, x);
    }

    // ---- 基本类型槽访问面 ----
    public boolean getBoolean(Object o, long offset) {
        return internal.getBoolean(o, offset);
    }

    public void putBoolean(Object o, long offset, boolean x) {
        internal.putBoolean(o, offset, x);
    }

    public byte getByte(Object o, long offset) {
        return internal.getByte(o, offset);
    }

    public void putByte(Object o, long offset, byte x) {
        internal.putByte(o, offset, x);
    }

    public short getShort(Object o, long offset) {
        return internal.getShort(o, offset);
    }

    public void putShort(Object o, long offset, short x) {
        internal.putShort(o, offset, x);
    }

    public char getChar(Object o, long offset) {
        return internal.getChar(o, offset);
    }

    public void putChar(Object o, long offset, char x) {
        internal.putChar(o, offset, x);
    }

    public int getInt(Object o, long offset) {
        return internal.getInt(o, offset);
    }

    public void putInt(Object o, long offset, int x) {
        internal.putInt(o, offset, x);
    }

    public long getLong(Object o, long offset) {
        return internal.getLong(o, offset);
    }

    public void putLong(Object o, long offset, long x) {
        internal.putLong(o, offset, x);
    }

    public float getFloat(Object o, long offset) {
        return internal.getFloat(o, offset);
    }

    public void putFloat(Object o, long offset, float x) {
        internal.putFloat(o, offset, x);
    }

    public double getDouble(Object o, long offset) {
        return internal.getDouble(o, offset);
    }

    public void putDouble(Object o, long offset, double x) {
        internal.putDouble(o, offset, x);
    }

    // ---- volatile 槽访问面（单线程 VM：与 plain 同型，VM 侧别名承载）----
    public boolean getBooleanVolatile(Object o, long offset) {
        return internal.getBooleanVolatile(o, offset);
    }

    public void putBooleanVolatile(Object o, long offset, boolean x) {
        internal.putBooleanVolatile(o, offset, x);
    }

    public byte getByteVolatile(Object o, long offset) {
        return internal.getByteVolatile(o, offset);
    }

    public void putByteVolatile(Object o, long offset, byte x) {
        internal.putByteVolatile(o, offset, x);
    }

    public short getShortVolatile(Object o, long offset) {
        return internal.getShortVolatile(o, offset);
    }

    public void putShortVolatile(Object o, long offset, short x) {
        internal.putShortVolatile(o, offset, x);
    }

    public char getCharVolatile(Object o, long offset) {
        return internal.getCharVolatile(o, offset);
    }

    public void putCharVolatile(Object o, long offset, char x) {
        internal.putCharVolatile(o, offset, x);
    }

    public int getIntVolatile(Object o, long offset) {
        return internal.getIntVolatile(o, offset);
    }

    public void putIntVolatile(Object o, long offset, int x) {
        internal.putIntVolatile(o, offset, x);
    }

    public long getLongVolatile(Object o, long offset) {
        return internal.getLongVolatile(o, offset);
    }

    public void putLongVolatile(Object o, long offset, long x) {
        internal.putLongVolatile(o, offset, x);
    }

    public float getFloatVolatile(Object o, long offset) {
        return internal.getFloatVolatile(o, offset);
    }

    public void putFloatVolatile(Object o, long offset, float x) {
        internal.putFloatVolatile(o, offset, x);
    }

    public double getDoubleVolatile(Object o, long offset) {
        return internal.getDoubleVolatile(o, offset);
    }

    public void putDoubleVolatile(Object o, long offset, double x) {
        internal.putDoubleVolatile(o, offset, x);
    }

    // ---- ordered 写面（JDK 8 putOrdered*）----
    public void putOrderedInt(Object o, long offset, int x) {
        internal.putOrderedInt(o, offset, x);
    }

    public void putOrderedLong(Object o, long offset, long x) {
        internal.putOrderedLong(o, offset, x);
    }

    // ---- CAS / 原子 RMW 面 ----
    public final boolean compareAndSwapInt(Object o, long offset, int expected, int x) {
        return internal.compareAndSetInt(o, offset, expected, x);
    }

    public final boolean compareAndSwapLong(Object o, long offset, long expected, long x) {
        return internal.compareAndSetLong(o, offset, expected, x);
    }

    public final int getAndAddInt(Object o, long offset, int delta) {
        return internal.getAndAddInt(o, offset, delta);
    }

    public final long getAndAddLong(Object o, long offset, long delta) {
        return internal.getAndAddLong(o, offset, delta);
    }

    public final int getAndSetInt(Object o, long offset, int newValue) {
        return internal.getAndSetInt(o, offset, newValue);
    }

    public final long getAndSetLong(Object o, long offset, long newValue) {
        return internal.getAndSetLong(o, offset, newValue);
    }

    // ---- 线程/栅栏面 ----
    public void park(boolean isAbsolute, long time) {
        internal.park(isAbsolute, time);
    }

    public void unpark(Thread thread) {
        internal.unpark(thread);
    }

    // JDK 9+ 形（jdk.unsupported 面）：入参 Object，非 Thread 抛 IAE（ART 语义）
    public void unpark(Object obj) {
        if (!(obj instanceof Thread)) {
            throw new IllegalArgumentException("Argument to unpark() was not a Thread");
        }
        internal.unpark((Thread) obj);
    }

    public void loadFence() {
        internal.loadFence();
    }

    public void storeFence() {
        internal.storeFence();
    }

    public void fullFence() {
        internal.fullFence();
    }

    public void storeStoreFence() {
        internal.storeStoreFence();
    }

    public void throwException(Throwable t) {
        internal.throwException(t);
    }

    // ---- 原生内存面（off-heap）：JDK 8 get/put(long) 形 + allocateMemory 族 +
    // copyMemory/setMemory 族（转发 jdk.internal.misc.Unsafe VM intrinsic；
    // 对象/数组形按 VM 逻辑 packed 字节视图展开）。----
    public byte getByte(long address) {
        return internal.getByte(address);
    }

    public void putByte(long address, byte x) {
        internal.putByte(address, x);
    }

    public short getShort(long address) {
        return internal.getShort(address);
    }

    public void putShort(long address, short x) {
        internal.putShort(address, x);
    }

    public char getChar(long address) {
        return internal.getChar(address);
    }

    public void putChar(long address, char x) {
        internal.putChar(address, x);
    }

    public int getInt(long address) {
        return internal.getInt(address);
    }

    public void putInt(long address, int x) {
        internal.putInt(address, x);
    }

    public long getLong(long address) {
        return internal.getLong(address);
    }

    public void putLong(long address, long x) {
        internal.putLong(address, x);
    }

    public float getFloat(long address) {
        return internal.getFloat(address);
    }

    public void putFloat(long address, float x) {
        internal.putFloat(address, x);
    }

    public double getDouble(long address) {
        return internal.getDouble(address);
    }

    public void putDouble(long address, double x) {
        internal.putDouble(address, x);
    }

    public long getAddress(long address) {
        return internal.getLong(address);
    }

    public void putAddress(long address, long x) {
        internal.putLong(address, x);
    }

    public long allocateMemory(long bytes) {
        return internal.allocateMemory(bytes);
    }

    public long reallocateMemory(long address, long bytes) {
        return internal.reallocateMemory(address, bytes);
    }

    public void freeMemory(long address) {
        internal.freeMemory(address);
    }

    public void setMemory(long address, long bytes, byte value) {
        internal.setMemory(address, bytes, value);
    }

    public void setMemory(Object o, long offset, long bytes, byte value) {
        internal.setMemory(o, offset, bytes, value);
    }

    public void copyMemory(long srcAddress, long destAddress, long bytes) {
        internal.copyMemory(srcAddress, destAddress, bytes);
    }

    public void copyMemory(Object srcBase, long srcOffset, Object destBase, long destOffset,
                           long bytes) {
        internal.copyMemory(srcBase, srcOffset, destBase, destOffset, bytes);
    }

    // ---- 类定义面（匿名类/保护域类定义，VM 类定义机器后置）----
    public Class<?> defineAnonymousClass(Class<?> hostClass, byte[] data, Object[] cpPatches) {
        throw noCode("defineAnonymousClass");
    }

    public Class<?> defineClass(String name, byte[] b, int off, int len,
                                ClassLoader loader, java.security.ProtectionDomain pd) {
        throw noCode("defineClass");
    }

    public int getLoadAverage(double[] loadavg, int nelems) {
        throw noCode("getLoadAverage");
    }

    public void invokeCleaner(java.nio.ByteBuffer buffer) {
        throw noCode("invokeCleaner");
    }
}
