package jdk.internal.misc;

/**
 * javac 编译面 stub（tools/security_unsafe_tests.sh 专用；不入 fixture dex——
 * d8 只收 sectest/**；运行期由 VM 内建 Ljdk/internal/misc/Unsafe; 承接）。
 * 签名与 core/builtins.cc Unsafe native 表逐项一致（对象槽族 + Int/Long 镜像）。
 */
public class Unsafe {

    public static native Unsafe getUnsafe();

    public native long objectFieldOffset(Class<?> c, String name);

    public native long objectFieldOffset(java.lang.reflect.Field f);

    public native Object getObject(Object o, long off);

    public native void putObject(Object o, long off, Object v);

    public native Object getObjectVolatile(Object o, long off);

    public native void putObjectVolatile(Object o, long off, Object v);

    public native void putOrderedObject(Object o, long off, Object v);

    public native Object getAndSetObject(Object o, long off, Object v);

    public native boolean compareAndSetObject(Object o, long off, Object e, Object v);

    public native Object compareAndExchangeObject(Object o, long off, Object e, Object v);

    public native int getInt(Object o, long off);

    public native void putInt(Object o, long off, int v);

    public native int getIntVolatile(Object o, long off);

    public native void putIntVolatile(Object o, long off, int v);

    public native void putOrderedInt(Object o, long off, int v);

    public native boolean compareAndSetInt(Object o, long off, int e, int v);

    public native int getAndSetInt(Object o, long off, int v);

    public native long getLong(Object o, long off);

    public native void putLong(Object o, long off, long v);

    public native long getLongVolatile(Object o, long off);

    public native void putLongVolatile(Object o, long off, long v);

    public native void putOrderedLong(Object o, long off, long v);

    public native boolean compareAndSetLong(Object o, long off, long e, long v);

    public native long getAndSetLong(Object o, long off, long v);
}
