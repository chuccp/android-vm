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
 * UTF-16 家族共享编解码器（UTF_16 / UTF_16BE / UTF_16LE 按参数区分）。
 * UTF_16 默认大端并识别/写入 BOM；LE/BE 形不处理 BOM。
 */
final class UTF16Decoder extends CharsetDecoder {

    private final boolean bigEndian;
    private final boolean useBom;

    UTF16Decoder(Charset cs, boolean bigEndian, boolean useBom) {
        super(cs, 0.5f, 1.0f);
        this.bigEndian = bigEndian;
        this.useBom = useBom;
    }

    @Override
    public CharBuffer decode(ByteBuffer in) throws CharacterCodingException {
        byte[] b = new byte[in.remaining()];
        in.get(b);
        boolean be = bigEndian;
        int off = 0;
        if (useBom && b.length >= 2) {
            if ((b[0] & 0xFF) == 0xFE && (b[1] & 0xFF) == 0xFF) {
                be = true;
                off = 2;
            } else if ((b[0] & 0xFF) == 0xFF && (b[1] & 0xFF) == 0xFE) {
                be = false;
                off = 2;
            }
        }
        int units = (b.length - off) / 2;
        char[] out = new char[units + 1];
        int n = 0;
        for (int i = 0; i < units; i++) {
            int hi = b[off + i * 2] & 0xFF;
            int lo = b[off + i * 2 + 1] & 0xFF;
            out[n++] = (char) (be ? ((hi << 8) | lo) : ((lo << 8) | hi));
        }
        if (((b.length - off) & 1) != 0) {
            if (malformedInputAction() == CodingErrorAction.REPORT) {
                throw new MalformedInputException(1);
            }
            out[n++] = '�';
        }
        return CharBuffer.wrap(out, 0, n);
    }
}

final class UTF16Encoder extends CharsetEncoder {

    private final boolean bigEndian;
    private final boolean useBom;

    UTF16Encoder(Charset cs, boolean bigEndian, boolean useBom) {
        super(cs, 2.0f, 4.0f);
        this.bigEndian = bigEndian;
        this.useBom = useBom;
    }

    @Override
    public ByteBuffer encode(CharBuffer in) {
        int chars = in.remaining();
        byte[] out = new byte[2 * chars + 2];
        int n = 0;
        if (useBom) {
            out[n++] = (byte) 0xFE;
            out[n++] = (byte) 0xFF;
        }
        while (in.hasRemaining()) {
            char c = in.get();
            int hi = (c >> 8) & 0xFF;
            int lo = c & 0xFF;
            if (bigEndian) {
                out[n++] = (byte) hi;
                out[n++] = (byte) lo;
            } else {
                out[n++] = (byte) lo;
                out[n++] = (byte) hi;
            }
        }
        byte[] trim = new byte[n];
        System.arraycopy(out, 0, trim, 0, n);
        return ByteBuffer.wrap(trim);
    }
}
