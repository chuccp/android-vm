package sun.security.action;

// 生成补源（sun-compat，随 runtime.dex 入包）：JBR 25 已删除 sun.security.action
// 包；vendored OpenJDK 11 源（Charset/InetAddress/URL 等）以 GetPropertyAction
// 取系统属性。OpenJDK 11 语义：PrivilegedAction<String> 包装 System.getProperty。
public class GetPropertyAction implements java.security.PrivilegedAction<String> {
    private final String theProp;
    private final String defaultVal;

    public GetPropertyAction(String prop) {
        this(prop, null);
    }

    public GetPropertyAction(String prop, String defaultVal) {
        this.theProp = prop;
        this.defaultVal = defaultVal;
    }

    @Override
    public String run() {
        String value = System.getProperty(theProp);
        return (value == null) ? defaultVal : value;
    }

    public static String getProperty(String prop) {
        return System.getProperty(prop);
    }

    public static String privilegedGetProperty(String prop) {
        return System.getProperty(prop);
    }

    public static String privilegedGetProperty(String prop, String defaultVal) {
        String value = System.getProperty(prop);
        return (value == null) ? defaultVal : value;
    }

    public static java.util.Properties privilegedGetProperties() {
        return System.getProperties();
    }

}
