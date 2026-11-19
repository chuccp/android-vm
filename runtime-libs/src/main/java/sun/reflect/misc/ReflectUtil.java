package sun.reflect.misc;

public class ReflectUtil {
    public static final String PROXY_PACKAGE = "com.sun.proxy";

    public static void checkPackageAccess(Class<?> clazz) {
    }

    public static void checkPackageAccess(ClassLoader cl, Class<?> clazz) {
    }

    public static void checkPackageAccess(String name) {
    }

    public static void checkProxyPackageAccess(ClassLoader cl, Class<?>[] interfaces) {
    }

    public static boolean needsPackageAccessCheck(ClassLoader loader,
                                                  ClassLoader callerLoader) {
        return false;
    }

    public static boolean isNonPublicProxyClass(Class<?> cls) {
        return false;
    }

    public static boolean isPackageAccessible(Class<?> cls) {
        return true;
    }
}
