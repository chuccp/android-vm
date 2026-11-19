package com.coke.target;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

public final class RuntimeReflectionFixture {
    private int value = 10;

    public int add(int delta) {
        return value + delta;
    }

    public int reflectionFlow() throws Exception {
        Class<?> type = Class.forName(
                "com.coke.target.RuntimeReflectionFixture");
        Constructor<?> constructor = type.getDeclaredConstructor();
        Object instance = constructor.newInstance();
        Method add = type.getDeclaredMethod("add", int.class);
        Object result = add.invoke(instance, 5);
        if (!(result instanceof Integer) || (Integer) result != 15) {
            return -1;
        }
        Field field = type.getDeclaredField("value");
        field.setAccessible(true);
        if (!Modifier.isPrivate(field.getModifiers()) ||
                field.isSynthetic() ||
                field.getType() != int.class ||
                field.getDeclaringClass() != type) {
            return -2;
        }
        boolean declaredFieldFound = false;
        for (Field candidate : type.getDeclaredFields()) {
            if ("value".equals(candidate.getName())) {
                declaredFieldFound = true;
            }
        }
        if (!declaredFieldFound) {
            return -2;
        }
        if ((Integer) field.get(instance) != 10) {
            return -2;
        }
        field.set(instance, 20);
        if ((Integer) add.invoke(instance, 2) != 22) {
            return -3;
        }
        return 93;
    }
}
