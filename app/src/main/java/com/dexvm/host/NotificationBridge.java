package com.dexvm.host;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.os.Build;
import android.util.Log;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;

/**
 * 宿主通知桥：guest lib NotificationManager 的发布/取消/渠道请求经
 * {@link NativeBridge#nativeNotificationTake()} 取出，用宿主真
 * {@link NotificationManager} 下发真系统通知（真通知，不伪造）。
 *
 * <p>线格式（小端，见 core/notification_bridge.h）：
 * i32 kind | i64 notifyId | i32 flags | i32 priority | i32 importance |
 * str tag | str channel | str channelName | str title | str text（str = i32 len + UTF-8）。
 */
public final class NotificationBridge {
    private static final String TAG = "DexVmNotify";

    private static final int KIND_NOTIFY = 0;
    private static final int KIND_CANCEL = 1;
    private static final int KIND_CANCEL_ALL = 2;
    private static final int KIND_CREATE_CHANNEL = 3;
    private static final int KIND_DELETE_CHANNEL = 4;

    private static Thread sThread;

    private NotificationBridge() {
    }

    /** 幂等启动（宿主主线程调用）。 */
    public static synchronized void start(Context context) {
        if (sThread != null || context == null) return;
        Context app = context.getApplicationContext();
        final Context ctx = app != null ? app : context;
        sThread = new Thread(() -> run(ctx), "dexvm-notify");
        sThread.setDaemon(true);
        sThread.start();
    }

    private static void run(Context ctx) {
        NotificationManager nm = (NotificationManager)
                ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) {
            Log.i(TAG, "no notification service");
            return;
        }
        Set<String> channels = new HashSet<>();
        while (true) {
            byte[] req;
            try {
                req = NativeBridge.nativeNotificationTake();
            } catch (Throwable t) {
                try {
                    Thread.sleep(500);
                } catch (InterruptedException e) {
                    return;
                }
                continue;
            }
            if (req == null) continue;
            try {
                dispatch(ctx, nm, channels, req);
            } catch (Throwable t) {
                Log.w(TAG, "dispatch failed", t);
            }
        }
    }

    private static void dispatch(Context ctx, NotificationManager nm, Set<String> channels,
                                 byte[] req) {
        ByteBuffer b = ByteBuffer.wrap(req).order(ByteOrder.LITTLE_ENDIAN);
        int kind = b.getInt();
        long id = b.getLong();
        int flags = b.getInt();
        int priority = b.getInt();
        int importance = b.getInt();
        String tag = readStr(b);
        String channel = readStr(b);
        String channelName = readStr(b);
        String title = readStr(b);
        String text = readStr(b);
        switch (kind) {
            case KIND_NOTIFY: {
                String ch = channel.isEmpty()
                        ? NotificationChannel.DEFAULT_CHANNEL_ID : channel;
                ensureChannel(nm, channels, ch, channelName, importance);
                Notification.Builder builder = Build.VERSION.SDK_INT >= 26
                        ? new Notification.Builder(ctx, ch)
                        : new Notification.Builder(ctx);
                builder.setSmallIcon(android.R.drawable.stat_notify_chat)
                        .setContentTitle(title == null ? "" : title)
                        .setContentText(text == null ? "" : text)
                        .setAutoCancel((flags & Notification.FLAG_AUTO_CANCEL) != 0)
                        .setOngoing((flags & Notification.FLAG_ONGOING_EVENT) != 0)
                        .setPriority(priority);
                Notification n = builder.build();
                if (tag.isEmpty()) {
                    nm.notify((int) id, n);
                } else {
                    nm.notify(tag, (int) id, n);
                }
                Log.i(TAG, "notify id=" + id + " channel=" + ch + " title=" + title);
                break;
            }
            case KIND_CANCEL:
                if (tag.isEmpty()) {
                    nm.cancel((int) id);
                } else {
                    nm.cancel(tag, (int) id);
                }
                break;
            case KIND_CANCEL_ALL:
                nm.cancelAll();
                break;
            case KIND_CREATE_CHANNEL:
                ensureChannel(nm, channels, channel, channelName, importance);
                break;
            case KIND_DELETE_CHANNEL:
                if (Build.VERSION.SDK_INT >= 26 && !channel.isEmpty()) {
                    nm.deleteNotificationChannel(channel);
                    channels.remove(channel);
                }
                break;
            default:
                break;
        }
    }

    private static void ensureChannel(NotificationManager nm, Set<String> channels,
                                      String id, String name, int importance) {
        if (id == null || id.isEmpty() || channels.contains(id)) return;
        try {
            if (Build.VERSION.SDK_INT >= 26) {
                CharSequence channelName = (name == null || name.isEmpty()) ? id : name;
                NotificationChannel ch = new NotificationChannel(id, channelName, importance);
                nm.createNotificationChannel(ch);
            }
            channels.add(id);
        } catch (Throwable t) {
            Log.w(TAG, "createChannel failed id=" + id, t);
        }
    }

    private static String readStr(ByteBuffer b) {
        if (b.remaining() < 4) return "";
        int len = b.getInt();
        if (len <= 0 || len > b.remaining()) return "";
        byte[] bytes = new byte[len];
        b.get(bytes);
        return new String(bytes, StandardCharsets.UTF_8);
    }
}
