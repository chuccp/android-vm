package sun.misc;

// javac 编译面 stub（tests/unsafe_legacy-src/stub，仅编译用，不入 fixture dex）。
// 运行期由 runtime.dex 的 runtime-libs/src/main/java/sun/misc/Unsafe.java
// （lib 壳）承载；本 stub 只需覆盖探针引用到的方法签名（与真类一致）。
// 常量不在此声明：探针经反射读运行期类的真实值（防 javac 内联假值）。
public final class Unsafe {

    private static final Unsafe theUnsafe = new Unsafe();

    private Unsafe() {
    }

    public static Unsafe getUnsafe() {
        return theUnsafe;
    }

    public long objectFieldOffset(java.lang.reflect.Field f) {
        return 0L;
    }

    public long staticFieldOffset(java.lang.reflect.Field f) {
        return 0L;
    }

    public Object staticFieldBase(java.lang.reflect.Field f) {
        return null;
    }

    public boolean shouldBeInitialized(Class<?> c) {
        return false;
    }

    public void ensureClassInitialized(Class<?> c) {
    }

    public int arrayBaseOffset(Class<?> arrayClass) {
        return 0;
    }

    public int arrayIndexScale(Class<?> arrayClass) {
        return 0;
    }

    public Object allocateInstance(Class<?> cls) throws InstantiationException {
        return null;
    }

    public Object getObject(Object o, long offset) {
        return null;
    }

    public void putObject(Object o, long offset, Object x) {
    }

    public Object getObjectVolatile(Object o, long offset) {
        return null;
    }

    public void putObjectVolatile(Object o, long offset, Object x) {
    }

    public boolean getBoolean(Object o, long offset) {
        return false;
    }

    public void putBoolean(Object o, long offset, boolean x) {
    }

    public byte getByte(Object o, long offset) {
        return 0;
    }

    public void putByte(Object o, long offset, byte x) {
    }

    public short getShort(Object o, long offset) {
        return 0;
    }

    public void putShort(Object o, long offset, short x) {
    }

    public char getChar(Object o, long offset) {
        return 0;
    }

    public void putChar(Object o, long offset, char x) {
    }

    public int getInt(Object o, long offset) {
        return 0;
    }

    public void putInt(Object o, long offset, int x) {
    }

    public long getLong(Object o, long offset) {
        return 0L;
    }

    public void putLong(Object o, long offset, long x) {
    }

    public float getFloat(Object o, long offset) {
        return 0f;
    }

    public void putFloat(Object o, long offset, float x) {
    }

    public double getDouble(Object o, long offset) {
        return 0d;
    }

    public void putDouble(Object o, long offset, double x) {
    }

    public boolean getBooleanVolatile(Object o, long offset) {
        return false;
    }

    public void putBooleanVolatile(Object o, long offset, boolean x) {
    }

    public byte getByteVolatile(Object o, long offset) {
        return 0;
    }

    public void putByteVolatile(Object o, long offset, byte x) {
    }

    public short getShortVolatile(Object o, long offset) {
        return 0;
    }

    public void putShortVolatile(Object o, long offset, short x) {
    }

    public char getCharVolatile(Object o, long offset) {
        return 0;
    }

    public void putCharVolatile(Object o, long offset, char x) {
    }

    public int getIntVolatile(Object o, long offset) {
        return 0;
    }

    public void putIntVolatile(Object o, long offset, int x) {
    }

    public long getLongVolatile(Object o, long offset) {
        return 0L;
    }

    public void putLongVolatile(Object o, long offset, long x) {
    }

    public float getFloatVolatile(Object o, long offset) {
        return 0f;
    }

    public void putFloatVolatile(Object o, long offset, float x) {
    }

    public double getDoubleVolatile(Object o, long offset) {
        return 0d;
    }

    public void putDoubleVolatile(Object o, long offset, double x) {
    }

    public void putOrderedObject(Object o, long offset, Object x) {
    }

    public void putOrderedInt(Object o, long offset, int x) {
    }

    public void putOrderedLong(Object o, long offset, long x) {
    }

    public boolean compareAndSwapObject(Object o, long offset, Object expected, Object x) {
        return false;
    }

    public boolean compareAndSwapInt(Object o, long offset, int expected, int x) {
        return false;
    }

    public boolean compareAndSwapLong(Object o, long offset, long expected, long x) {
        return false;
    }

    public Object getAndSetObject(Object o, long offset, Object x) {
        return null;
    }

    public int getAndSetInt(Object o, long offset, int newValue) {
        return 0;
    }

    public long getAndSetLong(Object o, long offset, long newValue) {
        return 0L;
    }

    public int getAndAddInt(Object o, long offset, int delta) {
        return 0;
    }

    public long getAndAddLong(Object o, long offset, long delta) {
        return 0L;
    }
}
