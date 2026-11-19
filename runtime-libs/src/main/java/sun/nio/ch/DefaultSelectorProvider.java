package sun.nio.ch;

// 生成补源（sun-compat）：sun.nio.ch.DefaultSelectorProvider（SelectorProvider 接线）。
// 指向平台实现单例（不得回调 SelectorProvider.provider——自递归即栈溢出）。
public class DefaultSelectorProvider {
    public static java.nio.channels.spi.SelectorProvider create() {
        return PollSelectorProvider.INSTANCE;
    }
}
