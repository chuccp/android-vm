package sun.security.util;

/**
 * javac 编译面 stub（tools/security_unsafe_tests.sh 专用；不入 fixture dex——
 * 运行期由 runtime.dex sun.security.util.Debug 承接）。签名与
 * runtime-libs/src/main/java/sun/security/util/Debug.java 一致。
 */
public class Debug {

    public static native Debug getInstance(String option);

    public static native Debug getInstance(String option, String prefix);

    public static native boolean isOn(String option);

    public native void println();

    public native void println(String message);

    public static native String toHexString(byte[] b);
}
