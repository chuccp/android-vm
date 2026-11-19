package jdk.internal.reflect;

import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;

public class ReflectionFactory {
    public static final class GetReflectionFactoryAction
            implements java.security.PrivilegedAction<ReflectionFactory> {
        @Override
        public ReflectionFactory run() {
            return new ReflectionFactory();
        }
    }

    public static ReflectionFactory getReflectionFactory() {
        return new ReflectionFactory();
    }

    public static void setLangReflectAccess(Object access) {
    }

    public static void infty() {
    }

    public ConstructorAccessor newConstructorAccessor(Constructor<?> c) {
        return null;
    }

    public MethodAccessor newMethodAccessor(Method m) {
        return null;
    }

    public MethodAccessor newFieldAccessor(java.lang.reflect.Field f, boolean override) {
        return null;
    }

    public Object[] getExecutableSharedParameterTypes(Executable ex) {
        return null;
    }

    public Class<?>[] getExecutableSharedParameterTypes0(Executable ex) {
        return null;
    }

    public static void setDelegate(Object delegate) {
    }

    public java.lang.reflect.Method copyMethod(java.lang.reflect.Method src) {
        return src;
    }

    public <T> java.lang.reflect.Constructor<T> copyConstructor(
            java.lang.reflect.Constructor<T> src) {
        return src;
    }

    public java.lang.reflect.Field copyField(java.lang.reflect.Field src) {
        return src;
    }

    public void throwException(Throwable t) throws Throwable {
        throw t;
    }

    public byte[] getExecutableTypeAnnotationBytes(Executable ex) {
        return null;
    }


    public java.lang.reflect.Constructor<?> newConstructorForSerialization(
            Class<?> cl) {
        try {
            return cl.getDeclaredConstructor();
        } catch (NoSuchMethodException e) {
            throw new IllegalStateException(e);
        }
    }

}
