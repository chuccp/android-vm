package android.os;

/**
 * binder 接口基（AOSP android.os.IInterface；§31.7 C 档 lib 接口面）。
 */
public interface IInterface {
    IBinder asBinder();
}
