package jdk.internal.misc;

public interface JavaLangInvokeAccess {
    Object newMemberName();
    Class<?> getDeclaringClass(Object memberName);
    String getName(Object memberName);
    java.lang.invoke.MethodType getMethodType(Object memberName);
    String getMethodDescriptor(Object memberName);
    boolean isNative(Object memberName);
    String toString(Object memberName);
}
