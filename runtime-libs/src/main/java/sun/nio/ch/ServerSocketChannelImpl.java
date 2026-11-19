package sun.nio.ch;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.SocketAddress;
import java.net.SocketOption;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.channels.spi.SelectorProvider;
import java.util.Collections;
import java.util.Set;

/** NIO 监听套接字通道（VM native 面；accept 非阻塞，就绪由选择器判定）。 */
public final class ServerSocketChannelImpl extends ServerSocketChannel {

    private int fd;
    private volatile boolean open = true;
    private volatile boolean bound;
    private ServerSocket socketView;

    ServerSocketChannelImpl(SelectorProvider provider) throws IOException {
        super(provider);
        fd = ChannelIO.openStreamSocket();
        // 监听套接字复用地址（AOSP 默认；快速重启考卷端口）
        ChannelIO.setSockOpt(fd, 1, 2);
    }

    int fd() {
        return fd;
    }

    public boolean isBound() {
        return bound;
    }

    @Override
    protected void implCloseSelectableChannel() throws IOException {
        open = false;
        int h = fd;
        fd = -1;
        if (h >= 0) {
            ChannelIO.closeFd(h);
        }
    }

    @Override
    protected void implConfigureBlocking(boolean block) throws IOException {
        if (fd >= 0) {
            ChannelIO.setBlocking(fd, block);
        }
    }

    @Override
    public ServerSocketChannel bind(SocketAddress local, int backlog) throws IOException {
        ensureOpen();
        int realBacklog = backlog > 0 ? backlog : 50;
        if (local != null) {
            if (!(local instanceof InetSocketAddress)) {
                throw new UnsupportedOperationException("unsupported address type");
            }
            ChannelIO.bind(fd, (InetSocketAddress) local);
        } else {
            ChannelIO.bind(fd, new InetSocketAddress(0));
        }
        ChannelIO.listen(fd, realBacklog);
        bound = true;
        return this;
    }

    @SuppressWarnings("unchecked")
    @Override
    public <T> ServerSocketChannel setOption(SocketOption<T> name, T value) throws IOException {
        ensureOpen();
        // 监听面选项：reuse 已默认开启；其余 no-op（netty 不校验回读）
        return this;
    }

    @Override
    public <T> T getOption(SocketOption<T> name) throws IOException {
        return null;
    }

    @Override
    public Set<SocketOption<?>> supportedOptions() {
        return Collections.emptySet();
    }

    @Override
    public ServerSocket socket() {
        if (socketView == null) {
            try {
                socketView = new ServerSocketView(this);
            } catch (IOException x) {
                throw new RuntimeException("ServerSocket view unavailable", x);
            }
        }
        return socketView;
    }

    @Override
    public SocketChannel accept() throws IOException {
        ensureOpen();
        if (!bound) {
            throw new java.nio.channels.NotYetBoundException();
        }
        int newFd = ChannelIO.accept(fd);
        if (newFd < 0) {
            return null;  // 无就绪连接（非阻塞语义；netty selector 驱动）
        }
        SocketChannelImpl sc = new SocketChannelImpl(provider());
        sc.adoptAcceptedFd(newFd);
        return sc;
    }

    @Override
    public SocketAddress getLocalAddress() throws IOException {
        ensureOpen();
        return ChannelIO.localAddress(fd);
    }

    private void ensureOpen() throws java.nio.channels.ClosedChannelException {
        if (!open) {
            throw new java.nio.channels.ClosedChannelException();
        }
    }

    @Override
    public String toString() {
        return "ServerSocketChannelImpl[fd=" + fd + ",bound=" + bound + "]";
    }

    /** java.net.ServerSocket 视图（netty 旧路径 socket().bind / 选项面）。 */
    static final class ServerSocketView extends ServerSocket {
        private final ServerSocketChannelImpl channel;
        private boolean reuseAddress = true;

        ServerSocketView(ServerSocketChannelImpl channel) throws IOException {
            super();
            this.channel = channel;
        }

        @Override
        public ServerSocketChannel getChannel() {
            return channel;
        }

        @Override
        public void bind(SocketAddress endpoint, int backlog) throws IOException {
            channel.bind(endpoint, backlog);
        }

        @Override
        public void bind(SocketAddress endpoint) throws IOException {
            channel.bind(endpoint, 50);
        }

        @Override
        public boolean isBound() {
            return channel.isBound();
        }

        @Override
        public java.net.SocketAddress getLocalSocketAddress() {
            try {
                return channel.getLocalAddress();
            } catch (IOException x) {
                return null;
            }
        }

        @Override
        public int getLocalPort() {
            java.net.SocketAddress a = getLocalSocketAddress();
            return a instanceof InetSocketAddress ? ((InetSocketAddress) a).getPort() : -1;
        }

        @Override
        public java.net.InetAddress getInetAddress() {
            java.net.SocketAddress a = getLocalSocketAddress();
            return a instanceof InetSocketAddress ? ((InetSocketAddress) a).getAddress() : null;
        }

        @Override
        public java.net.Socket accept() throws IOException {
            SocketChannel sc = channel.accept();
            return sc == null ? null : sc.socket();
        }

        @Override
        public void setReuseAddress(boolean on) {
            reuseAddress = on;
        }

        @Override
        public boolean getReuseAddress() {
            return reuseAddress;
        }

        @Override
        public void close() throws IOException {
            channel.close();
        }

        @Override
        public String toString() {
            return channel.toString();
        }
    }
}
