package android.os;

/**
 * 远程调用异常（AOSP android.os.RemoteException；§31.7 C 档 lib 异常壳）。
 */
public class RemoteException extends android.util.AndroidException {
    public RemoteException() {
        super();
    }

    public RemoteException(String message) {
        super(message);
    }

    public String getMessage() {
        return super.getMessage();
    }
}
