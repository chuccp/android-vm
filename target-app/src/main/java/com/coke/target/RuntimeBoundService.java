package com.coke.target;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.Process;

public final class RuntimeBoundService extends Service {
    static int events;
    static int unbindCount;

    @Override
    public void onCreate() {
        events |= 1;
        unbindCount = 0;
    }

    @Override
    public IBinder onBind(Intent intent) {
        events |= 2;
        return new EchoBinder();
    }

    @Override
    public boolean onUnbind(Intent intent) {
        events |= 4;
        return ++unbindCount == 1;
    }

    @Override
    public void onRebind(Intent intent) {
        events |= 16;
    }

    @Override
    public void onDestroy() {
        events |= 8;
    }

    private final class EchoBinder extends Binder {
        @Override
        protected boolean onTransact(
                int code,
                Parcel data,
                Parcel reply,
                int flags) {
            if (code != 5) {
                return false;
            }
            reply.writeInt(77);
            reply.writeInt(Binder.getCallingPid());
            reply.writeInt(Process.myPid());
            reply.writeInt(Binder.getCallingUid());
            reply.writeInt(
                    checkCallingPermission(
                            "com.coke.target.BIND_BOUND"));
            return true;
        }
    }
}
