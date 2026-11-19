package sun.security.action;

// 生成补源（sun-compat）：OpenJDK 11 语义，布尔系统属性读取。
public class GetBooleanAction implements java.security.PrivilegedAction<Boolean> {
    private final String theProp;

    public GetBooleanAction(String prop) {
        this.theProp = prop;
    }

    @Override
    public Boolean run() {
        return Boolean.getBoolean(theProp);
    }

    public boolean booleanValue() {
        return Boolean.getBoolean(theProp);
    }

    public static boolean getBoolean(String prop) {
        return Boolean.getBoolean(prop);
    }

    public static boolean privilegedGetBoolean(String prop) {
        return Boolean.getBoolean(prop);
    }
}
