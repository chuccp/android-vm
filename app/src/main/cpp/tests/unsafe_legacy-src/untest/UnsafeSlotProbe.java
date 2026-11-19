package untest;

import java.lang.reflect.Field;

import sun.misc.Unsafe;

/**
 * sun.misc.Unsafe 槽操作族（getInt/getObject/CAS + typed/volatile 镜像 +
 * 数组/静态槽面）。语义契约：与解释器 iget/iput 窄化互见（8 字节槽）。
 */
public class UnsafeSlotProbe {

    static class Holder {
        Object ref;
        long num;
        int i;
        boolean b;
        byte by;
        short s;
        char c;
        float f;
        double d;
    }

    static class SBox {
        static int sv = 3;
        static Object so;
    }

    static class Lazy {
        static int marker;

        static {
            marker = 5;
        }
    }

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    private static long off(Unsafe u, Class<?> k, String name) throws Exception {
        return u.objectFieldOffset(k.getDeclaredField(name));
    }

    private static int constOf(String name) throws Exception {
        return ((Integer) Unsafe.class.getDeclaredField(name).get(null)).intValue();
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();
        try {
            Unsafe u = Unsafe.getUnsafe();
            Holder h = new Holder();
            Object a = new Object();
            Object b = new Object();

            long refOff = off(u, Holder.class, "ref");
            long numOff = off(u, Holder.class, "num");
            long iOff = off(u, Holder.class, "i");

            // getInt/putInt + 与解释器 iget/iput 互见
            u.putInt(h, iOff, 5);
            sb.append(check(h.i == 5, "putInt-field-visible"));
            h.i = 6;
            sb.append(check(u.getInt(h, iOff) == 6, "getInt-field-written"));
            h.i = -7;
            sb.append(check(u.getInt(h, iOff) == -7, "getInt-negative"));

            // getObject/putObject
            u.putObject(h, refOff, a);
            sb.append(check(h.ref == a && u.getObject(h, refOff) == a, "putObject-getObject"));
            u.putObjectVolatile(h, refOff, b);
            sb.append(check(u.getObjectVolatile(h, refOff) == b, "get-putObjectVolatile"));
            u.putOrderedObject(h, refOff, a);
            sb.append(check(u.getObject(h, refOff) == a, "putOrderedObject"));

            // CAS 族（命中/未命中/落值）
            sb.append(check(u.compareAndSwapObject(h, refOff, a, b), "casObject-hit"));
            sb.append(check(h.ref == b, "casObject-stored"));
            sb.append(check(!u.compareAndSwapObject(h, refOff, a, a), "casObject-miss"));
            sb.append(check(u.getObject(h, refOff) == b, "casObject-miss-kept"));
            sb.append(check(u.compareAndSwapInt(h, iOff, -7, 60), "casInt-hit"));
            sb.append(check(h.i == 60, "casInt-stored"));
            sb.append(check(!u.compareAndSwapInt(h, iOff, -7, 6), "casInt-miss"));
            u.putLong(h, numOff, 9L);
            sb.append(check(u.compareAndSwapLong(h, numOff, 9L, 90L), "casLong-hit"));
            sb.append(check(h.num == 90L, "casLong-stored"));
            sb.append(check(!u.compareAndSwapLong(h, numOff, 9L, 1L), "casLong-miss"));

            // getAndSet / getAndAdd
            sb.append(check(u.getAndSetObject(h, refOff, a) == b && h.ref == a, "getAndSetObject"));
            sb.append(check(u.getAndSetInt(h, iOff, 7) == 60 && h.i == 7, "getAndSetInt"));
            sb.append(check(u.getAndAddInt(h, iOff, 3) == 7 && h.i == 10, "getAndAddInt"));
            sb.append(check(u.getAndAddLong(h, numOff, 5L) == 90L && h.num == 95L, "getAndAddLong"));
            sb.append(check(u.getAndSetLong(h, numOff, 1L) == 95L && h.num == 1L, "getAndSetLong"));

            // typed 槽面 + volatile 镜像 + 解释器窄化互见
            long bOff = off(u, Holder.class, "b");
            long byOff = off(u, Holder.class, "by");
            long sOff = off(u, Holder.class, "s");
            long cOff = off(u, Holder.class, "c");
            long fOff = off(u, Holder.class, "f");
            long dOff = off(u, Holder.class, "d");

            u.putBoolean(h, bOff, true);
            sb.append(check(h.b && u.getBoolean(h, bOff), "get-putBoolean"));
            h.b = false;
            sb.append(check(!u.getBooleanVolatile(h, bOff), "getBooleanVolatile-field"));
            u.putBooleanVolatile(h, bOff, true);
            sb.append(check(h.b, "putBooleanVolatile-field"));

            h.by = (byte) -1;
            sb.append(check(u.getByte(h, byOff) == (byte) -1, "getByte-sign"));
            u.putByte(h, byOff, (byte) 33);
            sb.append(check(h.by == (byte) 33, "putByte-field"));
            sb.append(check(u.getByteVolatile(h, byOff) == (byte) 33, "getByteVolatile"));

            h.s = (short) -2;
            sb.append(check(u.getShort(h, sOff) == (short) -2, "getShort-sign"));
            u.putShortVolatile(h, sOff, (short) 44);
            sb.append(check(h.s == (short) 44, "putShortVolatile-field"));

            h.c = 'Z';
            sb.append(check(u.getChar(h, cOff) == 'Z', "getChar"));
            u.putCharVolatile(h, cOff, 'Q');
            sb.append(check(h.c == 'Q' && u.getCharVolatile(h, cOff) == 'Q', "putCharVolatile-field"));

            h.f = 1.5f;
            sb.append(check(u.getFloat(h, fOff) == 1.5f, "getFloat"));
            u.putFloatVolatile(h, fOff, -2.25f);
            sb.append(check(h.f == -2.25f, "putFloatVolatile-field"));

            h.d = 3.5d;
            sb.append(check(u.getDouble(h, dOff) == 3.5d, "getDouble"));
            u.putDoubleVolatile(h, dOff, -4.5d);
            sb.append(check(h.d == -4.5d && u.getDoubleVolatile(h, dOff) == -4.5d,
                    "putDoubleVolatile-field"));

            u.putIntVolatile(h, iOff, 11);
            sb.append(check(h.i == 11 && u.getIntVolatile(h, iOff) == 11, "get-putIntVolatile"));
            u.putLongVolatile(h, numOff, 12L);
            sb.append(check(h.num == 12L && u.getLongVolatile(h, numOff) == 12L,
                    "get-putLongVolatile"));
            u.putOrderedInt(h, iOff, 13);
            sb.append(check(h.i == 13, "putOrderedInt-field"));
            u.putOrderedLong(h, numOff, 14L);
            sb.append(check(h.num == 14L, "putOrderedLong-field"));

            // 数组面（常量与 intrinsic 同值；int[]/Object[] 元素互见）
            int[] arr = new int[3];
            int base = u.arrayBaseOffset(int[].class);
            int scale = u.arrayIndexScale(int[].class);
            sb.append(check(base == constOf("ARRAY_INT_BASE_OFFSET"), "arrayBase-const-int"));
            sb.append(check(scale == constOf("ARRAY_INT_INDEX_SCALE"), "arrayScale-const-int"));
            sb.append(check(scale == 4, "arrayScale-int-logical"));
            sb.append(check(u.arrayIndexScale(byte[].class) == 1, "arrayScale-byte-logical"));
            sb.append(check(u.arrayIndexScale(char[].class) == 2, "arrayScale-char-logical"));
            u.putInt(arr, base + 1L * scale, 42);
            sb.append(check(arr[1] == 42, "array-putInt"));
            arr[0] = 7;
            sb.append(check(u.getInt(arr, base) == 7, "array-getInt"));

            Object[] oa = new Object[2];
            int obase = u.arrayBaseOffset(Object[].class);
            int oscale = u.arrayIndexScale(Object[].class);
            sb.append(check(obase == constOf("ARRAY_OBJECT_BASE_OFFSET"), "arrayBase-const-object"));
            sb.append(check(oscale == constOf("ARRAY_OBJECT_INDEX_SCALE"), "arrayScale-const-object"));
            u.putObject(oa, obase + 1L * oscale, a);
            sb.append(check(oa[1] == a, "array-putObject"));
            oa[0] = b;
            sb.append(check(u.getObject(oa, obase) == b, "array-getObject"));

            // 静态槽面（staticFieldBase/Offset 配对；先用主动引用触发 <clinit>）
            sb.append(check(SBox.sv == 3, "static-precondition"));
            Field sv = SBox.class.getDeclaredField("sv");
            Object sbase = u.staticFieldBase(sv);
            long soff = u.staticFieldOffset(sv);
            sb.append(check(u.getInt(sbase, soff) == 3, "static-getInt"));
            u.putInt(sbase, soff, 42);
            sb.append(check(SBox.sv == 42, "static-putInt-field"));
            SBox.sv = 5;
            sb.append(check(u.getInt(sbase, soff) == 5, "static-getInt-field"));

            Field so = SBox.class.getDeclaredField("so");
            Object obase2 = u.staticFieldBase(so);
            long ooff = u.staticFieldOffset(so);
            u.putObjectVolatile(obase2, ooff, a);
            sb.append(check(SBox.so == a, "static-putObjectVolatile-field"));
            sb.append(check(u.getObjectVolatile(obase2, ooff) == a, "static-getObjectVolatile"));

            // shouldBeInitialized / ensureClassInitialized（惰性 <clinit> 语义）
            sb.append(check(u.shouldBeInitialized(Lazy.class), "shouldBeInitialized-before"));
            u.ensureClassInitialized(Lazy.class);
            sb.append(check(Lazy.marker == 5, "ensureClassInitialized-runs-clinit"));
            sb.append(check(!u.shouldBeInitialized(Lazy.class), "shouldBeInitialized-after"));
        } catch (Throwable t) {
            sb.append("slot-thrown: FAIL " + t + "\n");
        }
        return sb.toString();
    }
}
