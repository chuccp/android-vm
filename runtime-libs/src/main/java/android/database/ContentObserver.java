package android.database;

import android.net.Uri;
import android.os.Handler;

/**
 * 内容变更观察者（AOSP android.database.ContentObserver 公开面）。
 * dispatchChange 串行化到 Handler（缺省创建线程直调）；onChange 可覆写。
 */
public abstract class ContentObserver {
    private final Handler mHandler;

    public ContentObserver(Handler handler) {
        mHandler = handler;
    }

    public final void dispatchChange(boolean selfChange) {
        dispatchChange(selfChange, null);
    }

    public final void dispatchChange(boolean selfChange, Uri uri) {
        if (mHandler == null) {
            onChange(selfChange, uri);
        } else {
            mHandler.post(new Runnable() {
                @Override
                public void run() {
                    onChange(selfChange, uri);
                }
            });
        }
    }

    public boolean deliverSelfNotifications() {
        return false;
    }

    public void onChange(boolean selfChange) {
        onChange(selfChange, null);
    }

    public void onChange(boolean selfChange, Uri uri) {
    }
}
