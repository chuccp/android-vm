package sun.nio.ch;

import java.io.IOException;
import java.net.ProtocolFamily;
import java.nio.channels.DatagramChannel;
import java.nio.channels.Pipe;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.channels.spi.AbstractSelector;
import java.nio.channels.spi.SelectorProvider;

/**
 * 平台选择器提供者（VM 面）：openSelector 可用；NIO 通道族（Socket/Server/
 * Datagram/Pipe）当前未实现——打开即 UnsupportedOperationException。
 * DefaultSelectorProvider.create 指向本单例（防 provider() 自递归）。
 */
public final class PollSelectorProvider extends SelectorProvider {

    public static final PollSelectorProvider INSTANCE = new PollSelectorProvider();

    private PollSelectorProvider() {
    }

    @Override
    public AbstractSelector openSelector() {
        return new PollSelectorImpl(this);
    }

    @Override
    public SocketChannel openSocketChannel() throws IOException {
        return new SocketChannelImpl(this);
    }

    @Override
    public ServerSocketChannel openServerSocketChannel() throws IOException {
        return new ServerSocketChannelImpl(this);
    }

    @Override
    public DatagramChannel openDatagramChannel() throws IOException {
        throw new UnsupportedOperationException("NIO DatagramChannel 未实现");
    }

    @Override
    public DatagramChannel openDatagramChannel(ProtocolFamily family) throws IOException {
        throw new UnsupportedOperationException("NIO DatagramChannel 未实现");
    }

    @Override
    public Pipe openPipe() throws IOException {
        throw new UnsupportedOperationException("NIO Pipe 未实现");
    }
}
