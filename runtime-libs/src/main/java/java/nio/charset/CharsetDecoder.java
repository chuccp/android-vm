package java.nio.charset;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;

/**
 * 解码器基类（VM 精简版：JDK CharsetDecoder 语义子集）。
 *
 * 与 JDK 的差异：decode(ByteBuffer) 由具体解码器直接实现（JDK 为 final 并
 * 落 decode(in,out,end)/flush 框架）；本实现保留 (ByteBuffer, CharBuffer,
 * boolean) 形以兼容 StringCoding 等 lib 消费面，内部按一次性解码 + 拷贝。
 */
public abstract class CharsetDecoder {

    private final Charset charset;
    private final float averageCharsPerByte;
    private final float maxCharsPerByte;
    private CodingErrorAction malformedInputAction = CodingErrorAction.REPORT;
    private CodingErrorAction unmappableCharacterAction = CodingErrorAction.REPORT;
    private String replacement = "�";

    protected CharsetDecoder(Charset cs, float averageCharsPerByte, float maxCharsPerByte) {
        if (averageCharsPerByte <= 0.0f) {
            throw new IllegalArgumentException("averageCharsPerByte");
        }
        if (maxCharsPerByte <= 0.0f) {
            throw new IllegalArgumentException("maxCharsPerByte");
        }
        this.charset = cs;
        this.averageCharsPerByte = averageCharsPerByte;
        this.maxCharsPerByte = maxCharsPerByte;
    }

    public final Charset charset() {
        return charset;
    }

    public final float averageCharsPerByte() {
        return averageCharsPerByte;
    }

    public final float maxCharsPerByte() {
        return maxCharsPerByte;
    }

    public final CodingErrorAction malformedInputAction() {
        return malformedInputAction;
    }

    public final CodingErrorAction unmappableCharacterAction() {
        return unmappableCharacterAction;
    }

    public final CharsetDecoder onMalformedInput(CodingErrorAction newAction) {
        if (newAction == null) {
            throw new IllegalArgumentException("null action");
        }
        malformedInputAction = newAction;
        return this;
    }

    public final CharsetDecoder onUnmappableCharacter(CodingErrorAction newAction) {
        if (newAction == null) {
            throw new IllegalArgumentException("null action");
        }
        unmappableCharacterAction = newAction;
        return this;
    }

    public final String replacement() {
        return replacement;
    }

    public final CharsetDecoder replaceWith(String newReplacement) {
        if (newReplacement == null) {
            throw new IllegalArgumentException("null replacement");
        }
        if (newReplacement.length() == 0) {
            throw new IllegalArgumentException("empty replacement");
        }
        replacement = newReplacement;
        return this;
    }

    public boolean isAutoDetecting() {
        return false;
    }

    public boolean isCharsetDetected() {
        return false;
    }

    public Charset detectedCharset() {
        return null;
    }

    public CharsetDecoder reset() {
        return this;
    }

    /** 一次性解码（消费 in 全部剩余字节）。 */
    public abstract CharBuffer decode(ByteBuffer in) throws CharacterCodingException;

    /** StringCoding 等 lib 消费面：一次性解码 + 拷贝到 out。 */
    public CoderResult decode(ByteBuffer in, CharBuffer out, boolean endOfInput) {
        try {
            CharBuffer decoded = decode(in);
            int n = decoded.remaining();
            if (n > out.remaining()) {
                int take = out.remaining();
                out.put(decoded.array(), decoded.arrayOffset() + decoded.position(), take);
                return CoderResult.OVERFLOW;
            }
            out.put(decoded.array(), decoded.arrayOffset() + decoded.position(), n);
            return CoderResult.UNDERFLOW;
        } catch (CharacterCodingException x) {
            return CoderResult.malformedForLength(1);
        }
    }

    public final CoderResult flush(CharBuffer out) {
        return CoderResult.UNDERFLOW;
    }
}
