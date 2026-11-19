package jdk.internal.org.objectweb.asm;

public class Type {
    public static final int VOID = 0;
    public static final int BOOLEAN = 1;
    public static final int CHAR = 2;
    public static final int BYTE = 3;
    public static final int SHORT = 4;
    public static final int INT = 5;
    public static final int FLOAT = 6;
    public static final int LONG = 7;
    public static final int DOUBLE = 8;
    public static final int ARRAY = 9;
    public static final int OBJECT = 10;
    public static final int METHOD = 11;

    public static final Type VOID_TYPE = getType(Void.TYPE);
    public static final Type BOOLEAN_TYPE = getType(Boolean.TYPE);
    public static final Type CHAR_TYPE = getType(Character.TYPE);
    public static final Type BYTE_TYPE = getType(Byte.TYPE);
    public static final Type SHORT_TYPE = getType(Short.TYPE);
    public static final Type INT_TYPE = getType(Integer.TYPE);
    public static final Type FLOAT_TYPE = getType(Float.TYPE);
    public static final Type LONG_TYPE = getType(Long.TYPE);
    public static final Type DOUBLE_TYPE = getType(Double.TYPE);

    private static void uoe() {
        throw new UnsupportedOperationException("compile stub");
    }

    private Type() {
    }

    public static Type getType(Class<?> clazz) { uoe(); return null; }
    public static Type getType(String typeDescriptor) { uoe(); return null; }
    public static Type getType(String typeDescriptor, int begin, int end) {
        uoe(); return null;
    }
    public static Type getObjectType(String internalName) { uoe(); return null; }
    public static Type getMethodType(Type returnType, Type... argumentTypes) {
        uoe(); return null;
    }
    public static Type getMethodType(String methodDescriptor) { uoe(); return null; }
    public static Type[] getArgumentTypes(String methodDescriptor) { uoe(); return null; }
    public static Type[] getArgumentTypes(Type methodType) { uoe(); return null; }
    public static Type getReturnType(String methodDescriptor) { uoe(); return null; }
    public static Type getReturnType(Type methodType) { uoe(); return null; }
    public static String getMethodDescriptor(Class<?> returnType, Class<?>... argumentTypes) {
        uoe(); return null;
    }
    public static String getMethodDescriptor(Type returnType, Type... argumentTypes) {
        uoe(); return null;
    }

    public int getSort() { uoe(); return 0; }
    public int getDimensions() { uoe(); return 0; }
    public Type getElementType() { uoe(); return null; }
    public String getClassName() { uoe(); return null; }
    public String getInternalName() { uoe(); return null; }
    public String getDescriptor() { uoe(); return null; }
    public String getDescriptor(StringBuilder stringBuilder) { uoe(); return null; }
    public int getSize() { uoe(); return 0; }
    public int getOpcode(int opcode) { uoe(); return 0; }
    public Type[] getArgumentTypes() { uoe(); return null; }
    public Type getReturnType() { uoe(); return null; }
    public int getArgumentsAndReturnSizes() { uoe(); return 0; }
    public int getArgsAndReturnSizes() { uoe(); return 0; }
    public String toString() { return "stub"; }
    public boolean equals(Object object) { return this == object; }
    public int hashCode() { return 0; }
}
