package android.content;

/**
 * VM SharedPreferences 落盘钩子（编译期脚手架）：实现由 libdexvm.so 内建类提供
 * （core/framework_builtins.cc 注册 intrinsic；builtin_classes.def 排除本类
 * 的 Java 版本，双实现零容忍——本文件仅让 lib 源码可编译）。
 *
 * <p>文件落点 = &lt;guestDataRoot&gt;/shared_prefs/&lt;name&gt;.xml（AOSP 路径同源），
 * 内容为 SharedPreferencesImpl 的 ASCII 线格式（base64 键值）。
 */
public final class VmPrefsStore {
    private VmPrefsStore() {
    }

    /** 读取同名单例的持久化内容；不存在返回 null。 */
    public static String read(String name) {
        throw new UnsupportedOperationException("VmPrefsStore.read: VM intrinsic");
    }

    /** 覆盖写入同名单例内容（目录不存在时创建）。 */
    public static void write(String name, String content) {
        throw new UnsupportedOperationException("VmPrefsStore.write: VM intrinsic");
    }
}
