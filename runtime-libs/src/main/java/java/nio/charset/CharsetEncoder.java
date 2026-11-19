package java.nio.charset;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;

/**
 * 编码器基类（VM 精简版：JDK CharsetEncoder 语义子集）。
 *
 * 与 JDK 的差异同 CharsetDecoder：encode(CharBuffer) 由具体编码器直接实现，
 * (CharBuffer, ByteBuffer, boolean) 形按一次性编码 + 拷贝兼容 lib 消费面。
 */
public abstract class CharsetEncoder {

    private final Charset charset;
    private final float averageBytesPerChar;
    private final float maxBytesPerChar;
    private CodingErrorAction malformedInputAction = CodingErrorAction.REPORT;
    private CodingErrorAction unmappableCharacterAction = CodingErrorAction.REPORT;
    private byte[] replacement = new byte[] { (byte) '?' };

    protected CharsetEncoder(Charset cs, float averageBytesPerChar, float maxBytesPerChar) {
        if (averageBytesPerChar <= 0.0f) {
            throw new IllegalArgumentException("averageBytesPerChar");
        }
        if (maxBytesPerChar <= 0.0f) {
            throw new IllegalArgumentException("maxBytesPerChar");
        }
        this.charset = cs;
        this.averageBytesPerChar = averageBytesPerChar;
        this.maxBytesPerChar = maxBytesPerChar;
    }

    public final Charset charset() {
        return charset;
    }

    public final float averageBytesPerChar() {
        return averageBytesPerChar;
    }

    public final float maxBytesPerChar() {
        return maxBytesPerChar;
    }

    public final CodingErrorAction malformedInputAction() {
        return malformedInputAction;
    }

    public final CodingErrorAction unmappableCharacterAction() {
        return unmappableCharacterAction;
    }

    public final CharsetEncoder onMalformedInput(CodingErrorAction newAction) {
        if (newAction == null) {
            throw new IllegalArgumentException("null action");
        }
        malformedInputAction = newAction;
        return this;
    }

    public final CharsetEncoder onUnmappableCharacter(CodingErrorAction newAction) {
        if (newAction == null) {
            throw new IllegalArgumentException("null action");
        }
        unmappableCharacterAction = newAction;
        return this;
    }

    public final byte[] replacement() {
        byte[] copy = new byte[replacement.length];
        System.arraycopy(replacement, 0, copy, 0, replacement.length);
        return copy;
    }

    public final CharsetEncoder replaceWith(byte[] newReplacement) {
        if (newReplacement == null) {
            throw new IllegalArgumentException("null replacement");
        }
        if (newReplacement.length == 0) {
            throw new IllegalArgumentException("empty replacement");
        }
        replacement = newReplacement;
        return this;
    }

    public boolean canEncode(char c) {
        return true;
    }

    public boolean canEncode(CharSequence cs) {
        for (int i = 0; i < cs.length(); i++) {
            if (!canEncode(cs.charAt(i))) {
                return false;
            }
        }
        return true;
    }

    public CharsetEncoder reset() {
        return this;
    }

    /** 一次性编码（消费 in 全部剩余字符）。 */
    public abstract ByteBuffer encode(CharBuffer in) throws CharacterCodingException;

    public final ByteBuffer encode(String str) throws CharacterCodingException {
        return encode(CharBuffer.wrap(str));
    }

    /** StringCoding 等 lib 消费面：一次性编码 + 拷贝到 out。 */
    public CoderResult encode(CharBuffer in, ByteBuffer out, boolean endOfInput) {
        try {
            ByteBuffer encoded = encode(in);
            int n = encoded.remaining();
            if (n > out.remaining()) {
                int take = out.remaining();
                out.put(encoded.array(), encoded.arrayOffset() + encoded.position(), take);
                return CoderResult.OVERFLOW;
            }
            out.put(encoded.array(), encoded.arrayOffset() + encoded.position(), n);
            return CoderResult.UNDERFLOW;
        } catch (CharacterCodingException x) {
            return CoderResult.unmappableForLength(1);
        }
    }

    public final CoderResult flush(ByteBuffer out) {
        return CoderResult.UNDERFLOW;
    }
}
