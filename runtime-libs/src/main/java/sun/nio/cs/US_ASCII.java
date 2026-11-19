package sun.nio.cs;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.MalformedInputException;
import java.nio.charset.UnmappableCharacterException;

/** US-ASCII（0x00..0x7F；越界按 malformed/unmappable 处理）。 */
public final class US_ASCII extends HistoricallyNamedCharset {

    public static final US_ASCII INSTANCE = new US_ASCII();

    public US_ASCII() {
        super("US-ASCII", new String[] { "ASCII", "ANSI_X3.4-1968", "ANSI_X3.4-1986",
                "ISO_646.irv:1991", "ISO646-US", "us", "IBM367", "cp367", "csASCII" });
    }

    @Override
    public String historicalName() {
        return "ASCII";
    }

    @Override
    public boolean contains(Charset cs) {
        return cs instanceof US_ASCII;
    }

    @Override
    public CharsetDecoder newDecoder() {
        return new Decoder(this);
    }

    @Override
    public CharsetEncoder newEncoder() {
        return new Encoder(this);
    }

    private static final class Decoder extends CharsetDecoder {
        Decoder(Charset cs) {
            super(cs, 1.0f, 1.0f);
        }

        @Override
        public CharBuffer decode(ByteBuffer in) throws CharacterCodingException {
            char[] out = new char[in.remaining()];
            int n = 0;
            while (in.hasRemaining()) {
                int c = in.get() & 0xFF;
                if (c < 0x80) {
                    out[n++] = (char) c;
                } else if (malformedInputAction() == CodingErrorAction.REPORT) {
                    throw new MalformedInputException(1);
                } else {
                    out[n++] = '�';
                }
            }
            return CharBuffer.wrap(out, 0, n);
        }
    }

    private static final class Encoder extends CharsetEncoder {
        Encoder(Charset cs) {
            super(cs, 1.0f, 1.0f);
        }

        @Override
        public boolean canEncode(char c) {
            return c < 0x80;
        }

        @Override
        public ByteBuffer encode(CharBuffer in) throws CharacterCodingException {
            byte[] out = new byte[in.remaining()];
            int n = 0;
            while (in.hasRemaining()) {
                char c = in.get();
                if (c < 0x80) {
                    out[n++] = (byte) c;
                } else if (unmappableCharacterAction() == CodingErrorAction.REPORT) {
                    throw new UnmappableCharacterException(1);
                } else {
                    out[n++] = (byte) '?';
                }
            }
            byte[] trim = new byte[n];
            System.arraycopy(out, 0, trim, 0, n);
            return ByteBuffer.wrap(trim);
        }
    }
}
