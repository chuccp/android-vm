package android.app;

/**
 * VM 通知钩子（编译期脚手架）：实现由 libdexvm.so 内建类提供
 * （core/framework_builtins.cc 注册 intrinsic；builtin_classes.def 排除本类
 * 的 Java 版本，双实现零容忍——本文件仅让 lib 源码可编译）。
 *
 * <p>请求经 core/notification_bridge.cc 队列 -> 宿主 NotificationBridge.java
 * 用真 NotificationManager 下发（真系统通知）。
 */
public final class VmNotifications {
    private VmNotifications() {
    }

    /** 发布通知（tag 可为空串；channel 为渠道 id）。 */
    public static void post(int id, String tag, String channel, String title, String text,
                            int flags, int priority) {
        throw new UnsupportedOperationException("VmNotifications.post: VM intrinsic");
    }

    public static void cancel(int id, String tag) {
        throw new UnsupportedOperationException("VmNotifications.cancel: VM intrinsic");
    }

    public static void cancelAll() {
        throw new UnsupportedOperationException("VmNotifications.cancelAll: VM intrinsic");
    }

    public static void createChannel(String id, String name, int importance) {
        throw new UnsupportedOperationException("VmNotifications.createChannel: VM intrinsic");
    }

    public static void deleteChannel(String id) {
        throw new UnsupportedOperationException("VmNotifications.deleteChannel: VM intrinsic");
    }
}
