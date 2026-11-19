package android.os;

/**
 * 操作取消异常（AOSP android.os.OperationCanceledException；§31.7 C 档 lib 异常壳）。
 */
public class OperationCanceledException extends RuntimeException {
    public OperationCanceledException() {
        super();
    }

    public OperationCanceledException(String message) {
        super(message);
    }
}
