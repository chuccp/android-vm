package android.app;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Parcel;
import android.os.Parcelable;

/**
 * 延迟意图（AOSP android.app.PendingIntent 公开面；§31.7 C 档 lib 骨架——跨进程
 * 触发归桥接层；本面身份字段态 + send 空转）。
 */
public final class PendingIntent implements Parcelable {

    public static final int FLAG_ONE_SHOT = 0x40000000;
    public static final int FLAG_NO_CREATE = 0x20000000;
    public static final int FLAG_CANCEL_CURRENT = 0x10000000;
    public static final int FLAG_UPDATE_CURRENT = 0x08000000;
    public static final int FLAG_IMMUTABLE = 0x04000000;
    public static final int FLAG_MUTABLE = 0x02000000;

    public interface OnFinished {
        void onSendFinished(PendingIntent pendingIntent, Intent intent, int resultCode,
                            String resultData, Bundle resultExtras);
    }

    private final Intent mIntent;
    private final int mType;

    private PendingIntent(Intent intent, int type) {
        this.mIntent = intent;
        this.mType = type;
    }

    public static PendingIntent getActivity(Context context, int requestCode, Intent intent,
                                            int flags) {
        return new PendingIntent(intent, 1);
    }

    public static PendingIntent getActivity(Context context, int requestCode, Intent intent,
                                            int flags, Bundle options) {
        return new PendingIntent(intent, 1);
    }

    public static PendingIntent getActivities(Context context, int requestCode,
                                             Intent[] intents, int flags) {
        return new PendingIntent(null, 2);
    }

    public static PendingIntent getActivities(Context context, int requestCode,
                                             Intent[] intents, int flags, Bundle options) {
        return new PendingIntent(null, 2);
    }

    public static PendingIntent getBroadcast(Context context, int requestCode, Intent intent,
                                             int flags) {
        return new PendingIntent(intent, 3);
    }

    public static PendingIntent getService(Context context, int requestCode, Intent intent,
                                           int flags) {
        return new PendingIntent(intent, 4);
    }

    public static PendingIntent getForegroundService(Context context, int requestCode,
                                                     Intent intent, int flags) {
        return new PendingIntent(intent, 5);
    }

    public Intent getIntent() {
        return this.mIntent;
    }

    public void send() {
    }

    public void send(int code) {
    }

    public void send(int code, OnFinished onFinished) {
    }

    public void send(Context context, int code, Intent intent) {
    }

    public void send(Context context, int code, Intent intent, OnFinished onFinished,
                     Handler scheduler) {
    }

    public void send(Context context, int code, Intent intent, OnFinished onFinished,
                     Handler scheduler, String requiredPermission) {
    }

    public void send(Context context, int code, Intent intent, OnFinished onFinished,
                     Handler scheduler, String requiredPermission,
                     Bundle options) {
    }

    public void cancel() {
    }

    public String getTargetPackage() {
        return null;
    }

    @Override
    public boolean equals(Object otherObj) {
        return this == otherObj;
    }

    @Override
    public int hashCode() {
        return System.identityHashCode(this);
    }

    @Override
    public String toString() {
        return "PendingIntent{" + this.mType + "}";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
    }
}
