package sun.security.util;

// 生成补源（sun-compat，随 runtime.dex 入包）：JBR 25 的 sun.security.util.
// SecurityConstants 不再暴露 legacy 权限常量（JEP 486 SecurityManager 收缩）；
// vendored OpenJDK 11 源（URL/Module/InetAddress/Permission 域等）仍引用。
// 语义对照 OpenJDK 11：同名 RuntimePermission/NetPermission/ReflectPermission。
public final class SecurityConstants {
    private SecurityConstants() {
    }

    public static final java.security.Permission GET_CLASSLOADER_PERMISSION =
            new java.lang.RuntimePermission("getClassLoader");
    public static final java.security.Permission CREATE_CLASSLOADER_PERMISSION =
            new java.lang.RuntimePermission("createClassLoader");
    public static final java.security.Permission GET_PD_PERMISSION =
            new java.lang.RuntimePermission("getProtectionDomain");
    public static final java.security.Permission CREATE_ACC_PERMISSION =
            new java.lang.RuntimePermission("createAccessControlContext");
    public static final java.security.Permission GET_COMBINER_PERMISSION =
            new java.lang.RuntimePermission("getSubjectCombiner");
    public static final java.security.Permission GET_STACK_TRACE_PERMISSION =
            new java.lang.RuntimePermission("getStackTrace");
    public static final java.security.Permission MODIFY_THREAD_PERMISSION =
            new java.lang.RuntimePermission("modifyThread");
    public static final java.security.Permission MODIFY_THREADGROUP_PERMISSION =
            new java.lang.RuntimePermission("modifyThreadGroup");
    public static final java.security.Permission STOP_THREAD_PERMISSION =
            new java.lang.RuntimePermission("stopThread");
    public static final java.security.Permission GET_COOKIEHANDLER_PERMISSION =
            new java.net.NetPermission("getCookieHandler");
    public static final java.security.Permission SET_COOKIEHANDLER_PERMISSION =
            new java.net.NetPermission("setCookieHandler");
    public static final java.security.Permission GET_RESPONSECACHE_PERMISSION =
            new java.net.NetPermission("getResponseCache");
    public static final java.security.Permission SET_RESPONSECACHE_PERMISSION =
            new java.net.NetPermission("setResponseCache");
    public static final java.security.Permission GET_PROXYSELECTOR_PERMISSION =
            new java.net.NetPermission("getProxySelector");
    public static final java.security.Permission SET_PROXYSELECTOR_PERMISSION =
            new java.net.NetPermission("setProxySelector");
    public static final java.security.Permission SET_SOCKETIMPL_PERMISSION =
            new java.net.NetPermission("setSocketImpl");
    public static final java.security.Permission SPECIFY_HANDLER_PERMISSION =
            new java.net.NetPermission("specifyStreamHandler");
    public static final java.security.Permission ACCESS_PERMISSION =
            new java.lang.RuntimePermission("accessDeclaredMembers");
    public static final java.security.Permission CHECK_MEMBER_ACCESS_PERMISSION =
            new java.lang.RuntimePermission("accessDeclaredMembers");
    public static final java.security.Permission SUBCLASS_IMPLEMENTATION_PERMISSION =
            new java.lang.reflect.ReflectPermission("suppressAccessChecks");
    public static final java.security.Permission GET_POLICY_PERMISSION =
            new java.lang.RuntimePermission("getPolicy");
    public static final java.security.Permission SET_POLICY_PERMISSION =
            new java.lang.RuntimePermission("setPolicy");

    // 权限动作常量（OpenJDK 11 SecurityConstants）
    public static final String FILE_READ_ACTION = "read";
    public static final String FILE_WRITE_ACTION = "write";
    public static final String FILE_EXECUTE_ACTION = "execute";
    public static final String FILE_DELETE_ACTION = "delete";
    public static final String FILE_READLINK_ACTION = "readlink";
    public static final String SOCKET_RESOLVE_ACTION = "resolve";
    public static final String SOCKET_CONNECT_ACTION = "connect";
    public static final String SOCKET_LISTEN_ACTION = "listen";
    public static final String SOCKET_ACCEPT_ACTION = "accept";
    public static final String SOCKET_CONNECT_ACCEPT_ACTION = "connect,accept";
    public static final String PROPERTY_READ_ACTION = "read";
    public static final String PROPERTY_WRITE_ACTION = "write";
    public static final String PROPERTY_RW_ACTION = "read,write";
    public static final java.security.Permission ALL_PERMISSION =
            new java.security.AllPermission();
}
