package android.os;

/**
 * 结果接收器（AOSP android.os.ResultReceiver；§31.7 C 档 lib 骨架——跨进程结果
 * 归桥接层；进程内直回调）。
 */
public class ResultReceiver implements Parcelable {

    /** 接收回调（AOSP ResultReceiver.OnResultListener）。 */
    public interface OnResultListener {
        void onResult(int resultCode, Bundle resultData);
    }

    private final OnResultListener mListener;

    public ResultReceiver(OnResultListener listener) {
        this.mListener = listener;
    }

    public void send(int resultCode, Bundle resultData) {
        if (this.mListener != null) {
            this.mListener.onResult(resultCode, resultData);
        }
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
    }
}
