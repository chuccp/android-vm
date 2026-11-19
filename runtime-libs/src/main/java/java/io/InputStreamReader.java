package java.io;

import java.nio.CharBuffer;
import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;

public class InputStreamReader extends Reader {
    private final InputStream in;
    private final String encoding;
    private byte[] bytes = new byte[1024];
    private int byteOffset;
    private int byteLimit;
    private char[] characters = new char[256];
    private int characterOffset;
    private int characterLimit;
    private boolean eof;
    private boolean closed;
    private boolean utf16Little;
    private boolean utf16BomChecked;

    public InputStreamReader(InputStream in) {
        super(in);
        if (in == null) throw new NullPointerException();
        this.in = in;
        this.encoding = normalize(Charset.defaultCharset().name());
        validateEncoding(encoding);
    }

    public InputStreamReader(InputStream in, String charsetName)
            throws UnsupportedEncodingException {
        super(in);
        if (in == null || charsetName == null) {
            throw new NullPointerException();
        }
        this.in = in;
        this.encoding = normalize(Charset.forName(charsetName).name());
        validateEncoding(encoding);
    }

    public InputStreamReader(InputStream in, Charset charset) {
        super(in);
        if (in == null || charset == null) {
            throw new NullPointerException();
        }
        this.in = in;
        this.encoding = normalize(charset.name());
        validateEncoding(encoding);
    }

    public InputStreamReader(InputStream in, CharsetDecoder decoder) {
        this(in, decoder != null ? decoder.charset() : null);
    }

    private static String normalize(String name) {
        String value = name.toUpperCase();
        if (value.equals("UTF8")) return "UTF-8";
        if (value.equals("ASCII")) return "US-ASCII";
        if (value.equals("LATIN1") || value.equals("ISO8859-1")) {
            return "ISO-8859-1";
        }
        if (value.equals("UTF16")) return "UTF-16";
        if (value.equals("UTF16LE")) return "UTF-16LE";
        if (value.equals("UTF16BE")) return "UTF-16BE";
        return value;
    }

    private static void validateEncoding(String encoding) {
        if (!encoding.equals("UTF-8") &&
                !encoding.equals("US-ASCII") &&
                !encoding.equals("ISO-8859-1") &&
                !encoding.equals("UTF-16") &&
                !encoding.equals("UTF-16LE") &&
                !encoding.equals("UTF-16BE")) {
            throw new java.nio.charset.UnsupportedCharsetException(encoding);
        }
    }

    private void ensureOpen() throws IOException {
        if (closed) throw new IOException("Stream closed");
    }

    public String getEncoding() {
        return closed ? null : encoding;
    }

    @Override
    public int read() throws IOException {
        char[] one = new char[1];
        int count = read(one, 0, 1);
        return count < 0 ? -1 : one[0];
    }

    @Override
    public int read(char[] target, int offset, int length)
            throws IOException {
        synchronized (lock) {
            ensureOpen();
            if (target == null) throw new NullPointerException();
            if (offset < 0 || length < 0 ||
                    offset > target.length - length) {
                throw new IndexOutOfBoundsException();
            }
            if (length == 0) return 0;
            while (characterOffset == characterLimit) {
                if (!decodeMore()) return -1;
            }
            int count = Math.min(
                    length, characterLimit - characterOffset);
            System.arraycopy(
                    characters, characterOffset,
                    target, offset, count);
            characterOffset += count;
            return count;
        }
    }

    private boolean decodeMore() throws IOException {
        characterOffset = 0;
        characterLimit = 0;
        while (characterLimit == 0) {
            if (eof && byteOffset == byteLimit) return false;
            if (byteOffset == byteLimit) {
                readBytes();
                if (byteOffset == byteLimit && !eof) continue;
            }
            int before = byteOffset;
            decodeAvailable();
            if (characterLimit == 0 && !eof &&
                    byteOffset == before) {
                readBytes();
            }
        }
        return true;
    }

    private void readBytes() throws IOException {
        if (byteOffset > 0) {
            System.arraycopy(
                    bytes, byteOffset, bytes, 0,
                    byteLimit - byteOffset);
            byteLimit -= byteOffset;
            byteOffset = 0;
        }
        if (byteLimit == bytes.length) {
            byte[] grown = new byte[bytes.length * 2];
            System.arraycopy(bytes, 0, grown, 0, byteLimit);
            bytes = grown;
        }
        int count = in.read(bytes, byteLimit, bytes.length - byteLimit);
        if (count < 0) {
            eof = true;
        } else {
            byteLimit += count;
        }
    }

    private void decodeAvailable() {
        if (encoding.equals("UTF-8")) {
            decodeUtf8();
        } else if (encoding.equals("US-ASCII")) {
            while (byteOffset < byteLimit) {
                int value = bytes[byteOffset++] & 0xff;
                append(value < 0x80 ? value : 0xfffd);
            }
        } else if (encoding.equals("ISO-8859-1")) {
            while (byteOffset < byteLimit) {
                append(bytes[byteOffset++] & 0xff);
            }
        } else {
            decodeUtf16();
        }
    }

    private void decodeUtf8() {
        while (byteOffset < byteLimit) {
            int first = bytes[byteOffset] & 0xff;
            if (first < 0x80) {
                byteOffset++;
                append(first);
                continue;
            }
            int width;
            int codePoint;
            if (first >= 0xc2 && first <= 0xdf) {
                width = 2;
                codePoint = first & 0x1f;
            } else if (first >= 0xe0 && first <= 0xef) {
                width = 3;
                codePoint = first & 0x0f;
            } else if (first >= 0xf0 && first <= 0xf4) {
                width = 4;
                codePoint = first & 0x07;
            } else {
                byteOffset++;
                append(0xfffd);
                continue;
            }
            if (byteOffset + width > byteLimit) {
                if (!eof) return;
                byteOffset = byteLimit;
                append(0xfffd);
                return;
            }
            boolean valid = true;
            for (int index = 1; index < width; index++) {
                int next = bytes[byteOffset + index] & 0xff;
                if ((next & 0xc0) != 0x80) {
                    valid = false;
                    break;
                }
                codePoint = (codePoint << 6) | (next & 0x3f);
            }
            if (valid) {
                int second = bytes[byteOffset + 1] & 0xff;
                valid = (width != 3 || first != 0xe0 || second >= 0xa0) &&
                        (width != 3 || first != 0xed || second <= 0x9f) &&
                        (width != 4 || first != 0xf0 || second >= 0x90) &&
                        (width != 4 || first != 0xf4 || second <= 0x8f);
            }
            if (!valid) {
                byteOffset++;
                append(0xfffd);
            } else {
                byteOffset += width;
                appendCodePoint(codePoint);
            }
        }
    }

    private void decodeUtf16() {
        if (encoding.equals("UTF-16") && !utf16BomChecked) {
            if (byteLimit - byteOffset < 2 && !eof) return;
            if (byteLimit - byteOffset >= 2) {
                int first = bytes[byteOffset] & 0xff;
                int second = bytes[byteOffset + 1] & 0xff;
                if (first == 0xfe && second == 0xff) {
                    utf16Little = false;
                    byteOffset += 2;
                } else if (first == 0xff && second == 0xfe) {
                    utf16Little = true;
                    byteOffset += 2;
                }
            }
            utf16BomChecked = true;
        }
        boolean little = encoding.equals("UTF-16LE") ||
                (encoding.equals("UTF-16") && utf16Little);
        while (byteLimit - byteOffset >= 2) {
            int first = readUtf16Unit(little, byteOffset);
            if (first >= 0xd800 && first <= 0xdbff &&
                    byteLimit - byteOffset >= 4) {
                int second = readUtf16Unit(little, byteOffset + 2);
                if (second >= 0xdc00 && second <= 0xdfff) {
                    byteOffset += 4;
                    append(first);
                    append(second);
                    continue;
                }
            }
            byteOffset += 2;
            append(first);
        }
        if (eof && byteOffset < byteLimit) {
            byteOffset = byteLimit;
            append(0xfffd);
        }
    }

    private int readUtf16Unit(boolean little, int offset) {
        int first = bytes[offset] & 0xff;
        int second = bytes[offset + 1] & 0xff;
        return little ? first | (second << 8) : (first << 8) | second;
    }

    private void appendCodePoint(int codePoint) {
        if (codePoint <= 0xffff) {
            append(codePoint);
        } else {
            int adjusted = codePoint - 0x10000;
            append(0xd800 + (adjusted >> 10));
            append(0xdc00 + (adjusted & 0x3ff));
        }
    }

    private void append(int character) {
        if (characterLimit == characters.length) {
            char[] grown = new char[characters.length * 2];
            System.arraycopy(
                    characters, 0, grown, 0, characterLimit);
            characters = grown;
        }
        characters[characterLimit++] = (char) character;
    }

    @Override
    public boolean ready() throws IOException {
        synchronized (lock) {
            ensureOpen();
            return characterOffset < characterLimit ||
                    byteOffset < byteLimit ||
                    in.available() > 0;
        }
    }

    @Override
    public int read(CharBuffer target) throws IOException {
        if (target == null) throw new NullPointerException();
        int length = Math.min(target.remaining(), 1024);
        if (length == 0) return 0;
        char[] buffer = new char[length];
        int count = read(buffer, 0, length);
        if (count > 0) target.put(buffer, 0, count);
        return count;
    }

    @Override
    public void close() throws IOException {
        synchronized (lock) {
            if (closed) return;
            closed = true;
            in.close();
        }
    }
}
