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

/** UTF-8（严格解码；REPLACE 面按 U+FFFD，编码不可映射按 '?'）。 */
public final class UTF_8 extends HistoricallyNamedCharset {

    public static final UTF_8 INSTANCE = new UTF_8();

    public UTF_8() {
        super("UTF-8", new String[] { "UTF8", "unicode-1-1-utf-8" });
    }

    @Override
    public String historicalName() {
        return "UTF8";
    }

    @Override
    public boolean contains(Charset cs) {
        return cs instanceof UTF_8;
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
            super(cs, 0.5f, 1.0f);
        }

        @Override
        public CharBuffer decode(ByteBuffer in) throws CharacterCodingException {
            byte[] b = new byte[in.remaining()];
            in.get(b);
            char[] out = new char[b.length + 1];
            int n = 0;
            int i = 0;
            while (i < b.length) {
                int c = b[i] & 0xFF;
                if (c < 0x80) {
                    out[n++] = (char) c;
                    i++;
                    continue;
                }
                int len;
                int cp;
                if ((c & 0xE0) == 0xC0) {
                    len = 2;
                    cp = c & 0x1F;
                } else if ((c & 0xF0) == 0xE0) {
                    len = 3;
                    cp = c & 0x0F;
                } else if ((c & 0xF8) == 0xF0) {
                    len = 4;
                    cp = c & 0x07;
                } else {
                    if (malformedInputAction() == CodingErrorAction.REPORT) {
                        throw new MalformedInputException(1);
                    }
                    out[n++] = '�';
                    i++;
                    continue;
                }
                boolean bad = i + len > b.length;
                if (!bad) {
                    for (int k = 1; k < len; k++) {
                        int cc = b[i + k] & 0xFF;
                        if ((cc & 0xC0) != 0x80) {
                            bad = true;
                            break;
                        }
                        cp = (cp << 6) | (cc & 0x3F);
                    }
                }
                if (!bad) {
                    if ((len == 2 && cp < 0x80)
                            || (len == 3 && cp < 0x800)
                            || (len == 4 && cp < 0x10000)
                            || cp > 0x10FFFF
                            || (cp >= 0xD800 && cp <= 0xDFFF)) {
                        bad = true;
                    }
                }
                if (bad) {
                    if (malformedInputAction() == CodingErrorAction.REPORT) {
                        throw new MalformedInputException(1);
                    }
                    out[n++] = '�';
                    i++;
                    continue;
                }
                if (cp >= 0x10000) {
                    int v = cp - 0x10000;
                    out[n++] = (char) (0xD800 + (v >> 10));
                    out[n++] = (char) (0xDC00 + (v & 0x3FF));
                } else {
                    out[n++] = (char) cp;
                }
                i += len;
            }
            return CharBuffer.wrap(out, 0, n);
        }
    }

    private static final class Encoder extends CharsetEncoder {
        Encoder(Charset cs) {
            super(cs, 1.1f, 3.0f);
        }

        @Override
        public boolean canEncode(char c) {
            return !(c >= '\uD800' && c <= '\uDFFF');
        }

        @Override
        public ByteBuffer encode(CharBuffer in) throws CharacterCodingException {
            int cap = in.remaining() * 3;
            byte[] out = new byte[cap];
            int n = 0;
            while (in.hasRemaining()) {
                char c = in.get();
                if (c < 0x80) {
                    out[n++] = (byte) c;
                } else if (c < 0x800) {
                    out[n++] = (byte) (0xC0 | (c >> 6));
                    out[n++] = (byte) (0x80 | (c & 0x3F));
                } else if (c >= '\uD800' && c <= '\uDBFF') {
                    if (in.hasRemaining()) {
                        char low = in.get(in.position());
                        if (low >= '\uDC00' && low <= '\uDFFF') {
                            in.get();
                            int cp = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                            out[n++] = (byte) (0xF0 | (cp >> 18));
                            out[n++] = (byte) (0x80 | ((cp >> 12) & 0x3F));
                            out[n++] = (byte) (0x80 | ((cp >> 6) & 0x3F));
                            out[n++] = (byte) (0x80 | (cp & 0x3F));
                            continue;
                        }
                    }
                    if (unmappableCharacterAction() == CodingErrorAction.REPORT) {
                        throw new UnmappableCharacterException(1);
                    }
                    out[n++] = (byte) '?';
                } else if (c >= '\uDC00' && c <= '\uDFFF') {
                    if (unmappableCharacterAction() == CodingErrorAction.REPORT) {
                        throw new UnmappableCharacterException(1);
                    }
                    out[n++] = (byte) '?';
                } else {
                    out[n++] = (byte) (0xE0 | (c >> 12));
                    out[n++] = (byte) (0x80 | ((c >> 6) & 0x3F));
                    out[n++] = (byte) (0x80 | (c & 0x3F));
                }
            }
            byte[] trim = new byte[n];
            System.arraycopy(out, 0, trim, 0, n);
            return ByteBuffer.wrap(trim);
        }
    }
}
