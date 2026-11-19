package com.coke.target;

import android.os.Binder;
import android.os.Parcel;
import android.os.Process;

/**
 * Verifies that Binder.transact dispatches onTransact as a logical Guest
 * coroutine rather than on the caller thread or an Android Binder thread.
 */
public final class RuntimeBinderFixture {
    private long callerTask;
    private long binderTask;
    private int binderPid;

    private final class EchoBinder extends Binder {
        @Override
        protected boolean onTransact(
                int code,
                Parcel data,
                Parcel reply,
                int flags) {
            binderTask = Thread.currentThread().getId();
            binderPid = Process.myPid();
            try {
                Thread.sleep(60L);
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                return false;
            }
            reply.writeInt(data.readInt() + 1);
            return true;
        }
    }

    public int roundTrip() throws android.os.RemoteException {
        callerTask = Thread.currentThread().getId();
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeInt(41);
            boolean delivered = new EchoBinder().transact(7, data, reply, 0);
            int value = reply.readInt();
            if (!delivered || value != 42) {
                return -1;
            }
            // binder 派发面判据：跨 GuestTask（1:1 线程语义）且同一 guest 进程
            // 身份（Process.myPid 与 caller 一致）。v1 的固定虚拟 pid 4200 是
            // 旧解释器身份面，Android 语义下 guest 进程 pid 即宿主进程 pid。
            if (callerTask == binderTask || binderPid != Process.myPid()) {
                return -2;
            }
            return value;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }
}
