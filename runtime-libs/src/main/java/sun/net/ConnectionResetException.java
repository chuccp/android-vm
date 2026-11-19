package sun.net;

// 生成补源（sun-compat）：sun.net.ConnectionResetException（java.net socket 读面
// ECONNRESET 语义载体——SocketInputStream.socketRead0 VM native 缝抛出、
// SocketInputStream.read / AbstractPlainSocketImpl.available 的 catch 转译
// "Connection reset"）。java.base 本体有此类；runtime.dex 需源覆盖入包
// （patch-module 口径同 java.net.* vendored 源），否则 catch 块类型解析落空。
public class ConnectionResetException extends RuntimeException {
    public ConnectionResetException(String msg) {
        super(msg);
    }
}
