package sun.nio.ch;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;

/**
 * 通道/选择器 native 面（VM 实现：core/builtins_io.cc）。
 * 通道 = 宿主 fd（阻塞/非阻塞按 fd 标志）；选择器 = poll 就绪 + 自唤醒管道。
 */
final class ChannelIO {

    private ChannelIO() {
    }

    static native int openStreamSocket() throws IOException;

    static native void bind(int fd, InetSocketAddress address) throws IOException;

    static native void listen(int fd, int backlog) throws IOException;

    /** 非阻塞 accept：无连接返回 -1。 */
    static native int accept(int fd) throws IOException;

    /** 非阻塞 connect：1=已连接 0=挂起。 */
    static native int connect(int fd, InetSocketAddress address) throws IOException;

    /** finishConnect：1=完成 0=仍挂起。 */
    static native int finishConnect(int fd) throws IOException;

    /** 返回 >0 字节数、0=暂无数据、-1=EOF。 */
    static native int readDirect(int fd, ByteBuffer buffer, int position, int length)
            throws IOException;

    static native int readHeap(int fd, byte[] array, int offset, int length) throws IOException;

    static native int writeDirect(int fd, ByteBuffer buffer, int position, int length)
            throws IOException;

    static native int writeHeap(int fd, byte[] array, int offset, int length) throws IOException;

    static native void closeFd(int fd);

    static native void setBlocking(int fd, boolean blocking);

    static native InetSocketAddress localAddress(int fd) throws IOException;

    static native InetSocketAddress remoteAddress(int fd) throws IOException;

    static native void shutdownInput(int fd);

    static native void shutdownOutput(int fd);

    /** option: 0=TCP_NODELAY 1=SO_KEEPALIVE 2=SO_REUSEADDR 3=SO_RCVBUF 4=SO_SNDBUF。 */
    static native void setSockOpt(int fd, int value, int option);

    static native int[] newWakeupPipe() throws IOException;

    static native void wakeupSignal(int fd);

    /** poll 就绪；timeoutMs<0 = 无限等待。ops 原地改写为就绪位。 */
    static native int select(int[] fds, int[] ops, int wakeupFd, long timeoutMs) throws IOException;
}
