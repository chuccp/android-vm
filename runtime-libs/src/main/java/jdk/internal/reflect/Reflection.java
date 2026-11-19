package jdk.internal.reflect;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.Set;

// 生成补源（sun-compat）：jdk.internal.reflect.Reflection（JBR 25 漂移面；vendored
// 只有 ReflectionFactory；JBR 25 IllegalAccessException 在 java.lang）。VM 无
// caller-sensitive 模块封装语义：getCallerClass 恒 null（lib ServiceLoader.
// checkCaller 已绑 native no-op，调用方不读返回值）；verify*/filter* 恒放行/不过滤。
public class Reflection {

    public static final Set<String> ALL_MEMBERS = Set.of();

    private Reflection() {
    }

    public static Class<?> getCallerClass() {
        return null;
    }

    public static Class<?> getCallerClass(int frames) {
        return null;
    }

    public static boolean verifyMemberAccess(Class<?> currentClass, Class<?> memberClass,
                                             Class<?> targetClass, int modifiers) {
        return true;
    }

    public static boolean verifyPublicMemberAccess(Class<?> memberClass, int modifiers) {
        return true;
    }

    public static boolean verifyModuleAccess(Module module, Class<?> svc) {
        return true;
    }

    public static void ensureMemberAccess(Class<?> currentClass, Class<?> memberClass,
                                          Object target, int modifiers) {
    }

    public static void ensureNativeAccess(Class<?> currentModule, Class<?> accessingModule,
                                          String accessingClassName, boolean isNative) {
    }

    public static int getClassAccessFlags(Class<?> c) {
        return c.getModifiers();
    }

    public static Field[] filterFields(Class<?> declaringClass, Field[] fields) {
        return fields;
    }

    public static Method[] filterMethods(Class<?> declaringClass, Method[] methods) {
        return methods;
    }

    public static void registerFieldsToFilter(Class<?> declaringClass, Set<String> fieldNames) {
    }

    public static void registerMethodsToFilter(Class<?> declaringClass, Set<String> methodNames) {
    }

    public static boolean isCallerSensitive(Method m) {
        return false;
    }

    public static boolean isTrustedFinalField(java.lang.reflect.Field f) {
        return false;
    }

    public static boolean areNestMates(Class<?> currentClass, Class<?> memberClass) {
        return true;
    }

    public static java.lang.IllegalAccessException newIllegalAccessException(Class<?> currentClass,
            Class<?> memberClass, Class<?> targetClass, int modifiers) {
        return new java.lang.IllegalAccessException("member access (VM 无模块封装)");
    }
}
