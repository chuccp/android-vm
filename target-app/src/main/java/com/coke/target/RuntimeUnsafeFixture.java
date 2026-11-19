package com.coke.target;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

public final class RuntimeUnsafeFixture {
    private int value = 7;
    private static int shared = 3;

    public int unsafeFlow() throws Exception {
        Class<?> unsafeType = Class.forName("sun.misc.Unsafe");
        Method getUnsafe = unsafeType.getDeclaredMethod("getUnsafe");
        Object unsafe = getUnsafe.invoke(null);

        Class<?> type = Class.forName(
                "com.coke.target.RuntimeUnsafeFixture");
        Field valueField = type.getDeclaredField("value");
        Method objectFieldOffset = unsafeType.getDeclaredMethod(
                "objectFieldOffset", Field.class);
        long offset = (Long) objectFieldOffset.invoke(unsafe, valueField);

        Constructor<?> constructor = type.getDeclaredConstructor();
        Object instance = constructor.newInstance();
        Method getInt = unsafeType.getDeclaredMethod(
                "getInt", Object.class, long.class);
        if ((Integer) getInt.invoke(unsafe, instance, offset) != 7) {
            return -1;
        }
        Method putInt = unsafeType.getDeclaredMethod(
                "putInt", Object.class, long.class, int.class);
        putInt.invoke(unsafe, instance, offset, 12);
        if ((Integer) getInt.invoke(unsafe, instance, offset) != 12) {
            return -2;
        }
        Method compareAndSwapInt = unsafeType.getDeclaredMethod(
                "compareAndSwapInt", Object.class, long.class,
                int.class, int.class);
        if (!(Boolean) compareAndSwapInt.invoke(
                unsafe, instance, offset, 12, 19)) {
            return -3;
        }
        if ((Integer) getInt.invoke(unsafe, instance, offset) != 19) {
            return -4;
        }

        Field sharedField = type.getDeclaredField("shared");
        Method staticFieldOffset = unsafeType.getDeclaredMethod(
                "staticFieldOffset", Field.class);
        long sharedOffset =
                (Long) staticFieldOffset.invoke(unsafe, sharedField);
        if ((Integer) getInt.invoke(unsafe, null, sharedOffset) != 3) {
            return -5;
        }
        putInt.invoke(unsafe, null, sharedOffset, 21);
        if (shared != 21) {
            return -6;
        }

        Method allocateInstance = unsafeType.getDeclaredMethod(
                "allocateInstance", Class.class);
        Object blank = allocateInstance.invoke(unsafe, type);
        if ((Integer) getInt.invoke(unsafe, blank, offset) != 0) {
            return -7;
        }
        return 94;
    }
}
