package java.lang.reflect;

import java.lang.annotation.Annotation;

public final class Field extends java.lang.reflect.AccessibleObject
        implements java.lang.reflect.Member {
    private String declaringClass;
    private String name;
    private String descriptor;
    private int modifiers;
    private boolean synthetic;
    private boolean accessible;

    public Class<?> getDeclaringClass() {
        return nativeClass(declaringClass);
    }

    public String getName() {
        return name;
    }

    public int getModifiers() {
        return modifiers;
    }

    public boolean isSynthetic() {
        return synthetic;
    }

    public Class<?> getType() {
        return nativeClass(descriptor);
    }

    public Type getGenericType() {
        return nativeClass(descriptor);
    }

    public boolean isEnumConstant() {
        return (modifiers & 0x4000) != 0;
    }

    @SuppressWarnings("unchecked")
    public <A extends Annotation> A getAnnotation(Class<A> type) {
        return (A) nativeAnnotation(this, type);
    }

    public <A extends Annotation> A getDeclaredAnnotation(Class<A> type) {
        return getAnnotation(type);
    }

    public boolean isAnnotationPresent(
            Class<? extends Annotation> type) {
        return type != null && getAnnotation(type) != null;
    }

    public Annotation[] getAnnotations() {
        return nativeAnnotations(this);
    }

    public Annotation[] getDeclaredAnnotations() {
        return nativeAnnotations(this);
    }

    public Object get(Object receiver) throws IllegalAccessException {
        return nativeGet(this, receiver);
    }

    public boolean getBoolean(Object receiver) throws IllegalAccessException {
        return ((Boolean) get(receiver)).booleanValue();
    }

    public byte getByte(Object receiver) throws IllegalAccessException {
        return ((Number) get(receiver)).byteValue();
    }

    public char getChar(Object receiver) throws IllegalAccessException {
        return ((Character) get(receiver)).charValue();
    }

    public short getShort(Object receiver) throws IllegalAccessException {
        return ((Number) get(receiver)).shortValue();
    }

    public int getInt(Object receiver) throws IllegalAccessException {
        return ((Number) get(receiver)).intValue();
    }

    public long getLong(Object receiver) throws IllegalAccessException {
        return ((Number) get(receiver)).longValue();
    }

    public float getFloat(Object receiver) throws IllegalAccessException {
        return ((Number) get(receiver)).floatValue();
    }

    public double getDouble(Object receiver) throws IllegalAccessException {
        return ((Number) get(receiver)).doubleValue();
    }

    public void set(Object receiver, Object value)
            throws IllegalAccessException {
        nativeSet(this, receiver, value);
    }

    public void setBoolean(Object receiver, boolean value) throws IllegalAccessException {
        set(receiver, Boolean.valueOf(value));
    }

    public void setByte(Object receiver, byte value) throws IllegalAccessException {
        set(receiver, Byte.valueOf(value));
    }

    public void setChar(Object receiver, char value) throws IllegalAccessException {
        set(receiver, Character.valueOf(value));
    }

    public void setShort(Object receiver, short value) throws IllegalAccessException {
        set(receiver, Short.valueOf(value));
    }

    public void setInt(Object receiver, int value) throws IllegalAccessException {
        set(receiver, Integer.valueOf(value));
    }

    public void setLong(Object receiver, long value) throws IllegalAccessException {
        set(receiver, Long.valueOf(value));
    }

    public void setFloat(Object receiver, float value) throws IllegalAccessException {
        set(receiver, Float.valueOf(value));
    }

    public void setDouble(Object receiver, double value) throws IllegalAccessException {
        set(receiver, Double.valueOf(value));
    }

    public void setAccessible(boolean accessible) {
        this.accessible = accessible;
    }

    public boolean isAccessible() {
        return accessible;
    }

    @Override
    public String toString() {
        int separator = descriptor.lastIndexOf('/');
        String type = separator < 0
                ? descriptor
                : descriptor.substring(separator + 1);
        return "Field(" + name + ", " + type + ")";
    }

    private static native Class<?> nativeClass(String descriptor);
    private static native Annotation nativeAnnotation(
            Field field, Class<?> type);
    private static native Annotation[] nativeAnnotations(Field field);
    private static native Object nativeGet(Field field, Object receiver);
    private static native void nativeSet(
            Field field, Object receiver, Object value);

}
