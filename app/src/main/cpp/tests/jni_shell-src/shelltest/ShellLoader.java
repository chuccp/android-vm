package shelltest;

/** System.load(path) 加载面（Java_* 解析前的 dlopen/JNI_OnLoad 链路）。 */
public final class ShellLoader {
    private ShellLoader() {
    }

    public static void load(String path) {
        System.load(path);
    }
}
