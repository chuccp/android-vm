package sun.nio.ch;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketAddress;
import java.net.SocketException;
import java.net.SocketOption;
import java.net.StandardSocketOptions;
import java.nio.ByteBuffer;
import java.nio.channels.AlreadyConnectedException;
import java.nio.channels.ClosedChannelException;
import java.nio.channels.ConnectionPendingException;
import java.nio.channels.NoConnectionPendingException;
import java.nio.channels.NotYetConnectedException;
import java.nio.channels.SocketChannel;
import java.nio.channels.spi.SelectorProvider;
import java.util.Collections;
import java.util.Set;

/** NIO 字节流套接字通道（VM native 面；非阻塞语义由 fd 标志承载）。 */
public final class SocketChannelImpl extends SocketChannel {

    private int fd;
    private final Object readLock = new Object();
    private final Object writeLock = new Object();
    private volatile boolean open = true;
    private volatile boolean connected;
    private volatile boolean connectionPending;
    private Socket socketView;

    SocketChannelImpl(SelectorProvider provider) throws IOException {
        super(provider);
        fd = ChannelIO.openStreamSocket();
    }

    /** 选择器就绪判定用（package 内）。 */
    int fd() {
        return fd;
    }

    /** 接受连接后的 fd 接管（ctor 已开临时 fd；ServerSocketChannelImpl.accept 用）。 */
    void adoptAcceptedFd(int acceptedFd) {
        if (fd >= 0) {
            ChannelIO.closeFd(fd);
        }
        fd = acceptedFd;
        connected = true;
        connectionPending = false;
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
    public SocketChannel bind(SocketAddress local) throws IOException {
        ensureOpen();
        if (local != null) {
            if (!(local instanceof InetSocketAddress)) {
                throw new UnsupportedOperationException("unsupported address type");
            }
            ChannelIO.bind(fd, (InetSocketAddress) local);
        }
        return this;
    }

    @SuppressWarnings("unchecked")
    @Override
    public <T> SocketChannel setOption(SocketOption<T> name, T value) throws IOException {
        ensureOpen();
        int intValue = (value instanceof Number) ? ((Number) value).intValue()
                                                 : (Boolean.TRUE.equals(value) ? 1 : 0);
        if (name == StandardSocketOptions.TCP_NODELAY) {
            ChannelIO.setSockOpt(fd, intValue == 0 ? 0 : 1, 0);
        } else if (name == StandardSocketOptions.SO_KEEPALIVE) {
            ChannelIO.setSockOpt(fd, intValue == 0 ? 0 : 1, 1);
        } else if (name == StandardSocketOptions.SO_REUSEADDR) {
            ChannelIO.setSockOpt(fd, intValue == 0 ? 0 : 1, 2);
        } else if (name == StandardSocketOptions.SO_RCVBUF) {
            ChannelIO.setSockOpt(fd, intValue, 3);
        } else if (name == StandardSocketOptions.SO_SNDBUF) {
            ChannelIO.setSockOpt(fd, intValue, 4);
        }
        return this;
    }

    @Override
    public <T> T getOption(SocketOption<T> name) throws IOException {
        return null;  // 取值面后置（netty 不依赖）
    }

    @Override
    public Set<SocketOption<?>> supportedOptions() {
        return Collections.emptySet();
    }

    @Override
    public Socket socket() {
        if (socketView == null) {
            socketView = new SocketView(this);
        }
        return socketView;
    }

    @Override
    public boolean isConnected() {
        return connected;
    }

    @Override
    public boolean isConnectionPending() {
        return connectionPending;
    }

    @Override
    public boolean connect(SocketAddress remote) throws IOException {
        ensureOpen();
        if (!(remote instanceof InetSocketAddress)) {
            throw new UnsupportedOperationException("unsupported address type");
        }
        if (connected) {
            throw new AlreadyConnectedException();
        }
        if (connectionPending) {
            throw new ConnectionPendingException();
        }
        int rc = ChannelIO.connect(fd, (InetSocketAddress) remote);
        if (rc == 1) {
            connected = true;
            return true;
        }
        connectionPending = true;
        return false;
    }

    @Override
    public boolean finishConnect() throws IOException {
        ensureOpen();
        if (connected) {
            return true;
        }
        if (!connectionPending) {
            throw new NoConnectionPendingException();
        }
        int rc = ChannelIO.finishConnect(fd);
        if (rc == 1) {
            connected = true;
            connectionPending = false;
            return true;
        }
        return false;
    }

    @Override
    public SocketAddress getRemoteAddress() throws IOException {
        ensureOpen();
        return ChannelIO.remoteAddress(fd);
    }

    @Override
    public SocketAddress getLocalAddress() throws IOException {
        ensureOpen();
        return ChannelIO.localAddress(fd);
    }

    @Override
    public int read(ByteBuffer dst) throws IOException {
        synchronized (readLock) {
            ensureOpen();
            ensureConnected();
            int position = dst.position();
            int length = dst.remaining();
            if (length == 0) {
                return 0;
            }
            int n;
            if (dst.hasArray()) {
                n = ChannelIO.readHeap(fd, dst.array(), dst.arrayOffset() + position, length);
            } else {
                n = ChannelIO.readDirect(fd, dst, position, length);
            }
            if (n > 0) {
                dst.position(position + n);
            }
            return n;
        }
    }

    @Override
    public long read(ByteBuffer[] dsts, int offset, int length) throws IOException {
        long total = 0;
        for (int i = offset; i < offset + length; i++) {
            int n = read(dsts[i]);
            if (n < 0) {
                return total == 0 ? -1 : total;
            }
            if (n == 0) {
                break;
            }
            total += n;
            if (!dsts[i].hasRemaining()) {
                continue;
            }
        }
        return total;
    }

    @Override
    public int write(ByteBuffer src) throws IOException {
        synchronized (writeLock) {
            ensureOpen();
            ensureConnected();
            int position = src.position();
            int length = src.remaining();
            if (length == 0) {
                return 0;
            }
            int n;
            if (src.hasArray()) {
                n = ChannelIO.writeHeap(fd, src.array(), src.arrayOffset() + position, length);
            } else {
                n = ChannelIO.writeDirect(fd, src, position, length);
            }
            if (n > 0) {
                src.position(position + n);
            }
            return n;
        }
    }

    @Override
    public long write(ByteBuffer[] srcs, int offset, int length) throws IOException {
        long total = 0;
        for (int i = offset; i < offset + length; i++) {
            int n = write(srcs[i]);
            if (n == 0) {
                break;
            }
            total += n;
        }
        return total;
    }

    @Override
    public SocketChannel shutdownInput() throws IOException {
        ensureOpen();
        ChannelIO.shutdownInput(fd);
        return this;
    }

    @Override
    public SocketChannel shutdownOutput() throws IOException {
        ensureOpen();
        ChannelIO.shutdownOutput(fd);
        return this;
    }

    private void ensureOpen() throws ClosedChannelException {
        if (!open) {
            throw new ClosedChannelException();
        }
    }

    private void ensureConnected() throws NotYetConnectedException {
        if (!connected) {
            throw new NotYetConnectedException();
        }
    }

    @Override
    public String toString() {
        return "SocketChannelImpl[fd=" + fd + ",connected=" + connected + "]";
    }

    /** java.net.Socket 视图（netty channel config 选项面）。 */
    static final class SocketView extends Socket {
        private final SocketChannelImpl channel;
        private boolean tcpNoDelay;
        private boolean keepAlive;
        private int soTimeout;

        SocketView(SocketChannelImpl channel) {
            this.channel = channel;
        }

        @Override
        public SocketChannel getChannel() {
            return channel;
        }

        @Override
        public boolean isConnected() {
            return channel.isConnected();
        }

        @Override
        public boolean isClosed() {
            return !channel.isOpen();
        }

        @Override
        public java.net.SocketAddress getRemoteSocketAddress() {
            try {
                return channel.getRemoteAddress();
            } catch (IOException x) {
                return null;
            }
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
        public void setTcpNoDelay(boolean on) throws SocketException {
            tcpNoDelay = on;
            ChannelIO.setSockOpt(channel.fd(), on ? 1 : 0, 0);
        }

        @Override
        public boolean getTcpNoDelay() {
            return tcpNoDelay;
        }

        @Override
        public void setKeepAlive(boolean on) throws SocketException {
            keepAlive = on;
            ChannelIO.setSockOpt(channel.fd(), on ? 1 : 0, 1);
        }

        @Override
        public boolean getKeepAlive() {
            return keepAlive;
        }

        @Override
        public void setSoTimeout(int timeout) {
            soTimeout = timeout;
        }

        @Override
        public int getSoTimeout() {
            return soTimeout;
        }

        @Override
        public void setReceiveBufferSize(int size) {
            ChannelIO.setSockOpt(channel.fd(), size, 3);
        }

        @Override
        public void setSendBufferSize(int size) {
            ChannelIO.setSockOpt(channel.fd(), size, 4);
        }

        @Override
        public void setReuseAddress(boolean on) throws SocketException {
            ChannelIO.setSockOpt(channel.fd(), on ? 1 : 0, 2);
        }

        @Override
        public void close() throws IOException {
            channel.close();
        }

        @Override
        public void shutdownInput() throws IOException {
            channel.shutdownInput();
        }

        @Override
        public void shutdownOutput() throws IOException {
            channel.shutdownOutput();
        }

        @Override
        public String toString() {
            return channel.toString();
        }
    }
}
