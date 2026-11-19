package sun.nio.cs;

import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;

/**
 * Charset.decode/encode 与 URI 等消费面的 coder 入口。
 * 本 VM 实现不做 ThreadLocal 缓存（每次新建；单 carrier 下无收益），
 * 语义与 JDK 一致：返回该 charset 的 decoder/encoder。
 */
public final class ThreadLocalCoders {

    private ThreadLocalCoders() {
    }

    public static CharsetDecoder decoderFor(Charset cs) {
        return cs.newDecoder();
    }

    public static CharsetEncoder encoderFor(Charset cs) {
        return cs.newEncoder();
    }
}
