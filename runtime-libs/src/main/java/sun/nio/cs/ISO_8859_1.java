package sun.nio.cs;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.UnmappableCharacterException;

/** ISO-8859-1（Latin-1）：字节与字符 0..255 一一对应。 */
public final class ISO_8859_1 extends HistoricallyNamedCharset {

    public static final ISO_8859_1 INSTANCE = new ISO_8859_1();

    public ISO_8859_1() {
        super("ISO-8859-1", new String[] { "ISO8859-1", "8859_1", "ISO_8859-1", "latin1", "l1",
                "iso-ir-100", "cp819", "ibm819", "819", "csISOLatin1" });
    }

    @Override
    public String historicalName() {
        return "ISO-8859-1";
    }

    @Override
    public boolean contains(Charset cs) {
        return cs instanceof ISO_8859_1;
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
        public CharBuffer decode(ByteBuffer in) {
            char[] out = new char[in.remaining()];
            int n = 0;
            while (in.hasRemaining()) {
                out[n++] = (char) (in.get() & 0xFF);
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
            return c <= 'ÿ';
        }

        @Override
        public ByteBuffer encode(CharBuffer in) throws CharacterCodingException {
            byte[] out = new byte[in.remaining()];
            int n = 0;
            while (in.hasRemaining()) {
                char c = in.get();
                if (c <= 'ÿ') {
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
