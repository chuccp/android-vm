package sun.nio.ch;

import java.io.IOException;
import java.nio.channels.CancelledKeyException;
import java.nio.channels.SelectableChannel;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.spi.AbstractSelectableChannel;
import java.nio.channels.spi.AbstractSelectionKey;
import java.nio.channels.spi.AbstractSelector;
import java.nio.channels.spi.SelectorProvider;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

/**
 * 选择器实现：注册/键集合语义 + poll(fd) 就绪判定（VM native 面）。
 * wakeup 经自唤醒管道打断 poll；select 返回就绪键数并置 readyOps。
 */
public final class PollSelectorImpl extends AbstractSelector {

    private final Set<SelectionKey> keys = new LinkedHashSet<>();
    private final Set<SelectionKey> selected = new LinkedHashSet<>();
    private int[] wakePipe;

    PollSelectorImpl(SelectorProvider provider) {
        super(provider);
    }

    private int wakeupFd() throws IOException {
        if (wakePipe == null) {
            wakePipe = ChannelIO.newWakeupPipe();
        }
        return wakePipe[0];
    }

    @Override
    protected void implCloseSelector() throws IOException {
        for (SelectionKey key : keys) {
            key.cancel();
        }
        keys.clear();
        selected.clear();
    }

    @Override
    protected SelectionKey register(AbstractSelectableChannel ch, int ops, Object att) {
        PollSelectionKey key = new PollSelectionKey(this, ch, att);
        key.attach(att);  // SelectionKey.attach 为 final 的私附件槽
        key.interestOps(ops);
        keys.add(key);
        return key;
    }

    @Override
    public Set<SelectionKey> keys() {
        return Collections.unmodifiableSet(keys);
    }

    @Override
    public Set<SelectionKey> selectedKeys() {
        return selected;
    }

    private int doSelect(long nativeTimeoutMs) throws IOException {
        List<SelectionKey> snapshot;
        synchronized (keys) {
            snapshot = new ArrayList<>(keys);
        }
        int count = snapshot.size();
        int[] fds = new int[count];
        int[] ops = new int[count];
        for (int i = 0; i < count; i++) {
            SelectionKey k = snapshot.get(i);
            if (!k.isValid()) {
                fds[i] = -1;
                ops[i] = 0;
                continue;
            }
            SelectableChannel ch = k.channel();
            if (ch instanceof SocketChannelImpl) {
                fds[i] = ((SocketChannelImpl) ch).fd();
            } else if (ch instanceof ServerSocketChannelImpl) {
                fds[i] = ((ServerSocketChannelImpl) ch).fd();
            } else {
                fds[i] = -1;
            }
            ops[i] = k.interestOps();
        }
        ChannelIO.select(fds, ops, wakeupFd(), nativeTimeoutMs);
        int ready = 0;
        for (int i = 0; i < count; i++) {
            SelectionKey k = snapshot.get(i);
            if (ops[i] != 0 && k.isValid()) {
                ((PollSelectionKey) k).setReadyOps(ops[i]);
                if (selected.add(k)) {
                    ready++;
                }
            } else {
                ((PollSelectionKey) k).setReadyOps(0);
            }
        }
        return selected.size();
    }

    @Override
    public int selectNow() throws IOException {
        return doSelect(0);
    }

    @Override
    public int select(long timeout) throws IOException {
        if (timeout < 0) {
            throw new IllegalArgumentException("negative timeout");
        }
        if (timeout == 0) {
            return doSelect(-1);  // JDK：0 = 无限阻塞
        }
        return doSelect(timeout);
    }

    @Override
    public int select() throws IOException {
        return select(0);
    }

    @Override
    public Selector wakeup() {
        try {
            if (wakePipe == null) {
                wakePipe = ChannelIO.newWakeupPipe();
            }
            ChannelIO.wakeupSignal(wakePipe[1]);
        } catch (IOException ignored) {
            // 唤醒失败退化为下一次超时
        }
        return this;
    }

    static final class PollSelectionKey extends AbstractSelectionKey {
        private final PollSelectorImpl selector;
        private final AbstractSelectableChannel channel;
        private final Object attachment;
        private volatile int interestOps;
        private volatile int readyOps;

        PollSelectionKey(PollSelectorImpl selector, AbstractSelectableChannel channel,
                Object attachment) {
            this.selector = selector;
            this.channel = channel;
            this.attachment = attachment;
        }

        void setReadyOps(int ops) {
            readyOps = ops;
        }

        @Override
        public SelectableChannel channel() {
            return channel;
        }

        @Override
        public Selector selector() {
            return selector;
        }

        @Override
        public int interestOps() {
            return interestOps;
        }

        @Override
        public SelectionKey interestOps(int ops) {
            if (!isValid()) {
                throw new CancelledKeyException();
            }
            if ((ops & ~(OP_READ | OP_WRITE | OP_CONNECT | OP_ACCEPT)) != 0) {
                throw new IllegalArgumentException("invalid interestOps: " + ops);
            }
            interestOps = ops;
            return this;
        }

        @Override
        public int readyOps() {
            return readyOps;
        }
    }
}
