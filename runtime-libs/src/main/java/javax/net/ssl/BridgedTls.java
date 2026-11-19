package javax.net.ssl;

/**
 * 宿主 TLS 桥接接缝（VM intrinsic：libdexvm.so 的 BindWellKnownNatives 绑定，
 * 实现见 app/src/main/cpp/core/tls_bridge.cc）。
 *
 * 语义口径（bridge-first）：TLS 握手/证书链/主机名校验/SNI/协议/cipher 全部由
 * 宿主真 TLS 栈完成；本类只传递字节流与连接生命周期。所有阻塞都经 VM 调度器
 * 协作切片等待，不占用 carrier。
 *
 * open 返回文本元数据（首行 {@code handle=<id>}，其余 key=value）：
 * protocol/cipher/subject/issuer/san=type|value/spki=alg|hex/addr=ip。
 */
final class BridgedTls {
    private BridgedTls() {
    }

    /** 建立宿主 TLS 连接；返回元数据串（connectTimeoutMs &lt;= 0 = 宿主默认）。 */
    static native String open(String host, int port, int connectTimeoutMs);

    /** 写入 data[off, off+len)；IO 异常经 native 上抛。 */
    static native void write(long handle, byte[] data, int off, int len);

    /** 读取至多 maxBytes；返回 null = EOF；SO_TIMEOUT 到期抛 SocketTimeoutException。 */
    static native byte[] read(long handle, int soTimeoutMs, int maxBytes);

    /** 关闭宿主连接（释放宿主 SSLSocket）。 */
    static native void close(long handle);
}
