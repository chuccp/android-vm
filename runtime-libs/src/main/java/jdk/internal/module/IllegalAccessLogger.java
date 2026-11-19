package jdk.internal.module;

public class IllegalAccessLogger {
    private static final IllegalAccessLogger LOGGER = new IllegalAccessLogger();

    public static IllegalAccessLogger illegalAccessLogger() {
        return LOGGER;
    }

    public static void logIfOpenedForIllegalAccess(Class<?> caller, Class<?> declaringClass,
                                                   java.util.function.Supplier<String> what) {
    }

    public static void logIfExportedForIllegalAccess(Class<?> caller, Class<?> declaringClass,
                                                     java.util.function.Supplier<String> what) {
    }

    public static void logIfOpenedForIllegalAccess(Class<?> caller, Class<?> declaringClass) {
    }

    public static void logIfOpenedForIllegalAccess(Class<?> caller, Class<?> declaringClass,
                                                   String member, String module) {
    }

    public static void logIfExportedForIllegalAccess(Class<?> caller, Class<?> declaringClass) {
    }

    public static void logIfExportedForIllegalAccess(Class<?> caller, Class<?> declaringClass,
                                                     String member, String module) {
    }

    public void logIfOpenedForIllegalAccess(Module module, String pkg, Class<?> caller) {
    }

    public void logIfExportedForIllegalAccess(Module module, String pkg, Class<?> caller) {
    }

    public boolean isOpenForIllegalAccess(Module module, String pkg) {
        return false;
    }

    public boolean isExportedForIllegalAccess(Module module, String pkg) {
        return false;
    }
}
