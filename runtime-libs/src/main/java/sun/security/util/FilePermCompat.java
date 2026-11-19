package sun.security.util;

// 生成补源（sun-compat）：JBR 25 删除 sun.security.util.FilePermCompat；
// vendored AccessControlContext/ProtectionDomain 引用。OpenJDK 11 语义子集。
public class FilePermCompat {
    public static final boolean nb = false;
    public static final boolean compat =
        System.getProperty("jdk.security.filePermCompat") != null;

    public static java.security.Permission newPermPlusAltPath(java.security.Permission perm) {
        return perm;
    }

    public static java.security.Permission newPermUsingAltPath(java.security.Permission perm) {
        return perm;
    }

    public static void checkRead(String path) {
    }

    public static void checkWrite(String path) {
    }

    public static void checkDelete(String path) {
    }

    public static void checkExec(String path) {
    }

    public static void checkRead(String path, Class<?> caller) {
    }

    public static void checkWrite(String path, Class<?> caller) {
    }

    public static void checkDelete(String path, Class<?> caller) {
    }

    public static void checkExec(String path, Class<?> caller) {
    }
}
