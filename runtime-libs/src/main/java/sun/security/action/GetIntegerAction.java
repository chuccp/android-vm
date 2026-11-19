package sun.security.action;

// 生成补源（sun-compat）：OpenJDK 11 语义，整型系统属性读取。
public class GetIntegerAction implements java.security.PrivilegedAction<Integer> {
    private final String theProp;
    private final int defaultVal;

    public GetIntegerAction(String prop) {
        this(prop, 0);
    }

    public GetIntegerAction(String prop, int defaultVal) {
        this.theProp = prop;
        this.defaultVal = defaultVal;
    }

    @Override
    public Integer run() {
        return Integer.getInteger(theProp, defaultVal);
    }

    public static int privilegedGetProperty(String prop, int defaultVal) {
        return Integer.getInteger(prop, defaultVal);
    }

    public static Integer privilegedGetInteger(String prop) {
        return Integer.getInteger(prop);
    }
}
