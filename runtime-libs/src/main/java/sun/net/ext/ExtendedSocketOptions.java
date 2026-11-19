package sun.net.ext;

// 生成补源（sun-compat）：sun.net.ext.ExtendedSocketOptions（JBR 25 形状漂移）。
public class ExtendedSocketOptions {
    public static final int SOCK_STREAM = 1;
    public static final int SOCK_DGRAM = 2;

    public static ExtendedSocketOptions getInstance() {
        return new ExtendedSocketOptions();
    }

    public static java.util.Set<java.net.SocketOption<?>> options(int type) {
        return java.util.Collections.emptySet();
    }

    public <T> boolean isOptionSupported(java.net.SocketOption<T> option) {
        return false;
    }

    public void setOption(java.io.FileDescriptor fd,
                          java.net.SocketOption<?> option, Object value)
            throws java.io.IOException {
    }

    public Object getOption(java.io.FileDescriptor fd,
                            java.net.SocketOption<?> option)
            throws java.io.IOException {
        return null;
    }

    public static void setOption(java.net.SocketImpl impl,
                                 java.net.SocketOption<?> option, Object value)
            throws java.io.IOException {
    }

    public static Object getOption(java.net.SocketImpl impl,
                                   java.net.SocketOption<?> option)
            throws java.io.IOException {
        return null;
    }

    public static java.util.Set<java.net.SocketOption<?>> options(
            java.net.SocketImpl impl) {
        return java.util.Collections.emptySet();
    }
}
