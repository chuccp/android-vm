package untest;

import java.lang.reflect.Field;

import sun.misc.Unsafe;

/**
 * sun.misc.Unsafe legacy 获取面（theUnsafe 反射 + getUnsafe 单例）。
 * 覆盖：Class.getDeclaredField("theUnsafe") -> setAccessible -> get(null)、
 * getUnsafe()、单例同一性、字段类型镜像。
 */
public class UnsafeAcquireProbe {

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();
        try {
            Field f = Unsafe.class.getDeclaredField("theUnsafe");
            sb.append(check(f != null, "acquire-getDeclaredField"));
            sb.append(check(f.getType() == Unsafe.class, "acquire-field-type"));
            f.setAccessible(true);
            Object reflected = f.get(null);
            sb.append(check(reflected instanceof Unsafe, "acquire-reflect-get"));
            Unsafe u = Unsafe.getUnsafe();
            sb.append(check(u != null, "acquire-getUnsafe"));
            sb.append(check(reflected == u, "acquire-singleton-identity"));
        } catch (Throwable t) {
            sb.append("acquire-thrown: FAIL " + t + "\n");
        }
        return sb.toString();
    }
}
