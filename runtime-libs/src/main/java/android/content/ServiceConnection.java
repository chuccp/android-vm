package android.content;

import android.os.IBinder;

/**
 * 服务连接回调（AOSP android.content.ServiceConnection 接口面）。
 * bindService 在 VM 内为本地 no-op，本接口只保证类型链接。
 */
public interface ServiceConnection {
    void onServiceConnected(ComponentName name, IBinder service);

    void onServiceDisconnected(ComponentName name);
}
