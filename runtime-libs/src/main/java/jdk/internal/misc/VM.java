package jdk.internal.misc;

// 生成补源（sun-compat）：jdk.internal.misc.VM（JBR 25 漂移：initializeFromArchive/
// initLevel 等）。vendored ImmutableCollections/ServiceLoader/System 引用；
// OpenJDK 11 语义子集。随 runtime.dex 入包（内建优先遮蔽同名 VM 内建若有）。
public class VM {
    private static int initLevel = 4;
    private static boolean booted = true;

    private VM() {
    }

    public static void initLevel(int level) {
        initLevel = level;
    }

    public static int initLevel() {
        return initLevel;
    }

    public static void awaitInitLevel(int level) throws InterruptedException {
    }

    public static boolean isBooted() {
        return booted;
    }

    public static boolean isShutdown() {
        return false;
    }

    public static boolean isModuleSystemInited() {
        return true;
    }

    public static boolean isSystemDomainLoader(ClassLoader loader) {
        return loader == null;
    }

    public static void initializeFromArchive(Class<?> cls) {
    }

    public static void initializeOSEnvironment() {
    }

    public static void initializeOSEnvironment(String[] args) {
    }

    public static void saveAndRemoveProperties(java.util.Properties props) {
    }

    public static void shutdown() {
    }

    public static String getSavedProperty(String key) {
        return null;
    }

    public static long maxDirectMemory() {
        return Long.MAX_VALUE;
    }

    public static long getNanoTimeAdjustment() {
        return 0L;
    }

    public static long getNanoTimeAdjustment(long offsetInSeconds) {
        return 0L;
    }

    public static void addFinalRefCount(int delta) {
    }

    public static ClassLoader latestUserDefinedLoader() {
        return null;
    }

    public static Thread.State toThreadState(int threadStatus) {
        return Thread.State.NEW;
    }
}
