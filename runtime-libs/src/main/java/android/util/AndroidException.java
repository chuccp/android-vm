package android.util;

/**
 * Android 异常基类（AOSP android.util.AndroidException；§31.7 C 档 lib 异常壳）。
 */
public class AndroidException extends RuntimeException {
    public AndroidException() {
        super();
    }

    public AndroidException(String name) {
        super(name);
    }

    public AndroidException(String name, Throwable cause) {
        super(name, cause);
    }

    public AndroidException(Exception cause) {
        super(cause);
    }
}
