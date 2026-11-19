package sectest;

import jdk.internal.misc.Unsafe;

/**
 * Unsafe 对象操作族探针（A 档 VM 接缝类 native 表）。
 * 覆盖：putObjectVolatile/getObjectVolatile、getObject/putObject、
 * putOrderedObject、compareAndSetObject（命中/未命中）、getAndSetObject、
 * compareAndExchangeObject + Int/Long 同型镜像族（putIntVolatile/getIntVolatile/
 * putOrderedInt/putLongVolatile/putLongVolatile/putOrderedLong）。
 * 目标：普通对象实例字段（objectFieldOffset(Class,String) 槽偏移）。
 */
public class UnsafeObjectProbe {

    static class Holder {
        Object ref;
        long num;
        int i;
    }

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();
        Unsafe u;
        try {
            u = Unsafe.getUnsafe();
        } catch (Throwable t) {
            return "unsafe-getUnsafe: FAIL " + t + "\n";
        }
        try {
            long refOff = u.objectFieldOffset(Holder.class, "ref");
            long numOff = u.objectFieldOffset(Holder.class, "num");
            long iOff = u.objectFieldOffset(Holder.class, "i");
            Holder h = new Holder();
            Object a = new Object();
            Object b = new Object();

            // 对象槽族
            u.putObjectVolatile(h, refOff, a);
            sb.append(check(u.getObjectVolatile(h, refOff) == a, "unsafe-getObjectVolatile"));
            u.putObject(h, refOff, b);
            sb.append(check(u.getObject(h, refOff) == b, "unsafe-getObject"));
            u.putOrderedObject(h, refOff, a);
            sb.append(check(u.getObject(h, refOff) == a, "unsafe-putOrderedObject"));
            sb.append(check(u.compareAndSetObject(h, refOff, a, b), "unsafe-casObject-hit"));
            sb.append(check(u.getObject(h, refOff) == b, "unsafe-casObject-store"));
            sb.append(check(!u.compareAndSetObject(h, refOff, a, a), "unsafe-casObject-miss"));
            sb.append(check(u.getAndSetObject(h, refOff, a) == b, "unsafe-getAndSetObject"));
            sb.append(check(u.compareAndExchangeObject(h, refOff, b, b) == a,
                    "unsafe-caeObject-old"));

            // Int 镜像族
            u.putIntVolatile(h, iOff, 7);
            sb.append(check(u.getIntVolatile(h, iOff) == 7, "unsafe-getIntVolatile"));
            u.putOrderedInt(h, iOff, 9);
            sb.append(check(u.getInt(h, iOff) == 9, "unsafe-putOrderedInt"));

            // Long 镜像族
            u.putLongVolatile(h, numOff, 123L);
            sb.append(check(u.getLongVolatile(h, numOff) == 123L, "unsafe-getLongVolatile"));
            u.putOrderedLong(h, numOff, 456L);
            sb.append(check(u.getLong(h, numOff) == 456L, "unsafe-putOrderedLong"));
        } catch (Throwable t) {
            sb.append("unsafe-ops: FAIL ").append(t).append('\n');
        }
        return sb.toString();
    }
}
