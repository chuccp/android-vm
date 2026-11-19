package android.os;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

/**
 * 远程回调表（AOSP android.os.RemoteCallbackList；§31.7 C 档 lib——注册表语义；
 * binder 通知归桥接层）。
 */
public class RemoteCallbackList<E extends IInterface> {

    /** 回调接口（AOSP RemoteCallbackList.Callback）。 */
    public interface Callback {
        void onCallbackDied(Object cookie);
    }

    private final Map<IBinder, E> mCallbacks = new HashMap<IBinder, E>();
    private final ArrayList<Object> mCookies = new ArrayList<Object>();

    public RemoteCallbackList() {
    }

    public boolean register(E callback) {
        return register(callback, null);
    }

    public boolean register(E callback, Object cookie) {
        if (callback == null) return false;
        IBinder binder = callback.asBinder();
        if (binder == null) return false;
        synchronized (this.mCallbacks) {
            if (this.mCallbacks.containsKey(binder)) return false;
            this.mCallbacks.put(binder, callback);
            this.mCookies.add(cookie);
        }
        return true;
    }

    public boolean unregister(E callback) {
        if (callback == null) return false;
        IBinder binder = callback.asBinder();
        if (binder == null) return false;
        synchronized (this.mCallbacks) {
            return this.mCallbacks.remove(binder) != null;
        }
    }

    public void kill() {
        synchronized (this.mCallbacks) {
            this.mCallbacks.clear();
            this.mCookies.clear();
        }
    }

    public void onCallbackDied(E callback, Object cookie) {
    }

    public int beginBroadcast() {
        return this.mCallbacks.size();
    }

    public E getBroadcastItem(int index) {
        int i = 0;
        for (E v : this.mCallbacks.values()) {
            if (i++ == index) return v;
        }
        return null;
    }

    public Object getBroadcastCookie(int index) {
        return index >= 0 && index < this.mCookies.size() ? this.mCookies.get(index) : null;
    }

    public void finishBroadcast() {
    }

    public int getRegisteredCallbackCount() {
        synchronized (this.mCallbacks) {
            return this.mCallbacks.size();
        }
    }

    public boolean isRegistered(E callback) {
        if (callback == null) return false;
        IBinder binder = callback.asBinder();
        return binder != null && this.mCallbacks.containsKey(binder);
    }
}
