package android.os;

/**
 * 消息信使（AOSP android.os.Messenger；§31.7 C 档 lib 骨架——跨进程消息归桥接层；
 * 进程内直投 Handler）。
 */
public class Messenger implements Parcelable {

    private final Handler mTarget;

    public Messenger(Handler target) {
        this.mTarget = target;
    }

    public Messenger(IBinder target) {
        this.mTarget = null;
    }

    public void send(Message message) {
        if (this.mTarget != null) {
            this.mTarget.sendMessage(message);
        }
    }

    public IBinder getBinder() {
        return null;
    }

    public static Messenger readMessengerOrNull(Parcel in) {
        return null;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
    }
}
