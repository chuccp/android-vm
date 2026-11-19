package android.app;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * 通知服务（AOSP android.app.NotificationManager 公开面）。
 *
 * <p>宿主桥接：发布/取消/渠道请求经 {@link VmNotifications} 入 VM 队列，
 * 宿主 {@code NotificationBridge} 用真 NotificationManager 下发真系统通知。
 * 渠道表在 guest 侧同步维护（getNotificationChannel(s) 语义面）。
 */
public class NotificationManager {
    public static final String ACTION_NOTIFICATION_LISTENER_SETTINGS =
            "android.settings.ACTION_NOTIFICATION_LISTENER_SETTINGS";

    private static final Map<String, NotificationChannel> sChannels = new LinkedHashMap<>();

    public void notify(int id, Notification notification) {
        notify(null, id, notification);
    }

    public void notify(String tag, int id, Notification notification) {
        if (notification == null) return;
        String channel = notification.channelId;
        if (channel == null || channel.isEmpty()) {
            channel = NotificationChannel.DEFAULT_CHANNEL_ID;
        }
        String title = notification.contentTitle != null
                ? notification.contentTitle.toString() : "";
        String text = notification.contentText != null
                ? notification.contentText.toString() : "";
        try {
            android.util.Log.i("DexVmGuestNotify", "post id=" + id + " channel=" + channel
                    + " title=" + title);
        } catch (Throwable ignored) {
        }
        VmNotifications.post(id, tag != null ? tag : "", channel, title, text,
                notification.flags, notification.priority);
    }

    public void cancel(int id) {
        cancel(null, id);
    }

    public void cancel(String tag, int id) {
        VmNotifications.cancel(id, tag != null ? tag : "");
    }

    public void cancelAll() {
        VmNotifications.cancelAll();
    }

    public boolean areNotificationsEnabled() {
        return true;
    }

    public int getImportance() {
        return NotificationChannel.IMPORTANCE_DEFAULT;
    }

    public NotificationChannel getNotificationChannel(String channelId) {
        synchronized (sChannels) {
            return sChannels.get(channelId);
        }
    }

    public List<NotificationChannel> getNotificationChannels() {
        synchronized (sChannels) {
            return new ArrayList<>(sChannels.values());
        }
    }

    public void createNotificationChannel(NotificationChannel channel) {
        if (channel == null || channel.getId() == null) return;
        synchronized (sChannels) {
            sChannels.put(channel.getId(), channel);
        }
        String name = channel.getName() != null ? channel.getName().toString() : channel.getId();
        VmNotifications.createChannel(channel.getId(), name, channel.getImportance());
    }

    public void createNotificationChannels(List<NotificationChannel> channels) {
        if (channels == null) return;
        for (NotificationChannel channel : channels) {
            createNotificationChannel(channel);
        }
    }

    public void deleteNotificationChannel(String channelId) {
        if (channelId == null) return;
        synchronized (sChannels) {
            sChannels.remove(channelId);
        }
        VmNotifications.deleteChannel(channelId);
    }
}
