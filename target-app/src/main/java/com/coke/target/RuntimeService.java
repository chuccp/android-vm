package com.coke.target;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

public final class RuntimeService extends Service {
    private int startCount;

    @Override
    public void onCreate() {
        super.onCreate();
        startCount = 0;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        startCount++;
        return TargetEntry.serviceResult(startCount);
    }

    @Override
    public void onDestroy() {
        startCount = 0;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
