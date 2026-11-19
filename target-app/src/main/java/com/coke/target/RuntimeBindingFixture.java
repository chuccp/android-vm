package com.coke.target;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.os.Parcel;

public final class RuntimeBindingFixture
        implements ServiceConnection, IBinder.DeathRecipient {
    private boolean connected;
    private boolean dead;
    private int reply;
    private int callerPid;
    private int servicePid;
    private int callingUid;
    private int permission;

    public boolean connect(Context context) {
        RuntimeBoundService.events = 0;
        connected = false;
        dead = false;
        reply = 0;
        Intent intent = new Intent();
        intent.setClassName("com.coke.target", "com.coke.target.RuntimeBoundService");
        return context.bindService(intent, this, 0);
    }

    public void disconnect(Context context) {
        context.unbindService(this);
    }

    @Override
    public void onServiceConnected(ComponentName name, IBinder service) {
        if (!"com.coke.target".equals(name.getPackageName())
                || !"com.coke.target.RuntimeBoundService".equals(name.getClassName())) {
            return;
        }
        Parcel data = Parcel.obtain();
        Parcel response = Parcel.obtain();
        try {
            boolean delivered = service.transact(5, data, response, 0);
            if (delivered) {
                reply = response.readInt();
                callerPid = response.readInt();
                servicePid = response.readInt();
                callingUid = response.readInt();
                permission = response.readInt();
                service.linkToDeath(this, 0);
                connected = true;
            }
        } catch (android.os.RemoteException ignored) {
            connected = false;
        } finally {
            data.recycle();
            response.recycle();
        }
    }

    @Override
    public void onServiceDisconnected(ComponentName name) {
        connected = false;
    }

    @Override
    public void binderDied() {
        dead = true;
        connected = false;
    }

    public int testStatus() {
        return (connected ? 0x10000 : 0)
                | (reply << 8)
                | RuntimeBoundService.events;
    }

    public String testDetails() {
        return callerPid + "," + servicePid + "," + callingUid
                + "," + permission + "," + RuntimeBoundService.events
                + "," + (dead ? "true" : "false");
    }
}
