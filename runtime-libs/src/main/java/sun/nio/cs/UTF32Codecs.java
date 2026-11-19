package sun.nio.cs;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.MalformedInputException;

/**
 * UTF-32 家族共享编解码器（UTF_32BE / UTF_32LE 按参数区分）。
 * 固定 4 字节/码点；不处理 BOM（OkHttp 的 BomAwareReader 自行跳 BOM）。
 * 与 UTF16Codecs 同形：覆写 decode/encode 便捷面（VM lib 的抽象面契约）。
 */
final class UTF32Decoder extends CharsetDecoder {

    private final boolean bigEndian;

    UTF32Decoder(Charset cs, boolean bigEndian) {
        super(cs, 0.25f, 1.0f);
        this.bigEndian = bigEndian;
    }

    @Override
    public CharBuffer decode(ByteBuffer in) throws CharacterCodingException {
        byte[] b = new byte[in.remaining()];
        in.get(b);
        int units = b.length / 4;
        char[] out = new char[Math.max(units * 2, 1)];
        int n = 0;
        for (int i = 0; i < units; i++) {
            int o = i * 4;
            int cp = bigEndian
                    ? ((b[o] & 0xFF) << 24) | ((b[o + 1] & 0xFF) << 16)
                            | ((b[o + 2] & 0xFF) << 8) | (b[o + 3] & 0xFF)
                    : ((b[o + 3] & 0xFF) << 24) | ((b[o + 2] & 0xFF) << 16)
                            | ((b[o + 1] & 0xFF) << 8) | (b[o] & 0xFF);
            if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                if (malformedInputAction() == CodingErrorAction.REPORT) {
                    throw new MalformedInputException(4);
                }
                cp = 0xFFFD;
            }
            if (cp <= 0xFFFF) {
                out[n++] = (char) cp;
            } else {
                cp -= 0x10000;
                out[n++] = (char) (0xD800 + (cp >> 10));
                out[n++] = (char) (0xDC00 + (cp & 0x3FF));
            }
        }
        if ((b.length & 3) != 0) {
            if (malformedInputAction() == CodingErrorAction.REPORT) {
                throw new MalformedInputException(b.length & 3);
            }
            out[n++] = '�';
        }
        return CharBuffer.wrap(out, 0, n);
    }
}

final class UTF32Encoder extends CharsetEncoder {

    private final boolean bigEndian;

    UTF32Encoder(Charset cs, boolean bigEndian) {
        super(cs, 4.0f, 4.0f);
        this.bigEndian = bigEndian;
    }

    @Override
    public ByteBuffer encode(CharBuffer in) {
        byte[] out = new byte[in.remaining() * 4 + 4];
        int n = 0;
        while (in.hasRemaining()) {
            char c = in.get();
            int cp;
            if (Character.isHighSurrogate(c)) {
                if (in.hasRemaining()) {
                    char low = in.get();
                    if (Character.isLowSurrogate(low)) {
                        cp = Character.toCodePoint(c, low);
                    } else {
                        cp = 0xFFFD;
                        // 非低代理：已消费字符按替换语义处理（与 REPORT 无关，
                        // encode 便捷面不抛 CharacterCodingException）。
                    }
                } else {
                    cp = 0xFFFD;
                }
            } else if (Character.isLowSurrogate(c)) {
                cp = 0xFFFD;
            } else {
                cp = c;
            }
            if (bigEndian) {
                out[n++] = (byte) (cp >> 24);
                out[n++] = (byte) (cp >> 16);
                out[n++] = (byte) (cp >> 8);
                out[n++] = (byte) cp;
            } else {
                out[n++] = (byte) cp;
                out[n++] = (byte) (cp >> 8);
                out[n++] = (byte) (cp >> 16);
                out[n++] = (byte) (cp >> 24);
            }
        }
        byte[] trim = new byte[n];
        System.arraycopy(out, 0, trim, 0, n);
        return ByteBuffer.wrap(trim);
    }
}
