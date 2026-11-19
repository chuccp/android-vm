package com.dexvm.host;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

/** isolated worker 占位：D1 起承载 MVContext + ART Runtime 生命周期。 */
public class MvWorkerService extends Service {
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
