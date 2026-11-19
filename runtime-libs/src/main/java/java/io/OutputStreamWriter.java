package java.io;

import java.nio.CharBuffer;
import java.nio.charset.Charset;
import java.nio.charset.CharsetEncoder;

public class OutputStreamWriter extends Writer {
    private final OutputStream out;
    private final String encoding;
    private boolean closed;
    private boolean started;
    private char pendingHighSurrogate;

    public OutputStreamWriter(
            OutputStream out, String charsetName)
            throws UnsupportedEncodingException {
        super(out);
        if (out == null || charsetName == null) {
            throw new NullPointerException();
        }
        this.out = out;
        this.encoding = normalize(Charset.forName(charsetName).name());
        validateEncoding(encoding);
    }

    public OutputStreamWriter(OutputStream out) {
        super(out);
        if (out == null) throw new NullPointerException();
        this.out = out;
        this.encoding = normalize(Charset.defaultCharset().name());
        validateEncoding(encoding);
    }

    public OutputStreamWriter(OutputStream out, Charset charset) {
        super(out);
        if (out == null || charset == null) {
            throw new NullPointerException();
        }
        this.out = out;
        this.encoding = normalize(charset.name());
        validateEncoding(encoding);
    }

    public OutputStreamWriter(OutputStream out, CharsetEncoder encoder) {
        this(out, encoder != null ? encoder.charset() : null);
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
    public void write(int character) throws IOException {
        char[] one = {(char) character};
        write(one, 0, 1);
    }

    @Override
    public void write(char[] source, int offset, int length)
            throws IOException {
        synchronized (lock) {
            ensureOpen();
            if (source == null) throw new NullPointerException();
            if (offset < 0 || length < 0 ||
                    offset > source.length - length) {
                throw new IndexOutOfBoundsException();
            }
            for (int index = 0; index < length; index++) {
                encodeCharacter(source[offset + index] & 0xffff);
            }
        }
    }

    @Override
    public void write(String source, int offset, int length)
            throws IOException {
        if (source == null) throw new NullPointerException();
        if (offset < 0 || length < 0 ||
                offset > source.length() - length) {
            throw new IndexOutOfBoundsException();
        }
        char[] characters = new char[length];
        source.getChars(offset, offset + length, characters, 0);
        write(characters, 0, length);
    }

    private void encodeCharacter(int character) throws IOException {
        if (encoding.equals("UTF-16LE") ||
                encoding.equals("UTF-16BE") ||
                encoding.equals("UTF-16")) {
            encodeUtf16(character);
        } else if (encoding.equals("UTF-8")) {
            encodeUtf8(character);
        } else if (encoding.equals("ISO-8859-1")) {
            out.write(character <= 0xff ? character : '?');
        } else {
            out.write(character < 0x80 ? character : '?');
        }
    }

    private void encodeUtf8(int character) throws IOException {
        if (pendingHighSurrogate != 0) {
            if (character >= 0xdc00 && character <= 0xdfff) {
                int codePoint = 0x10000 +
                        ((pendingHighSurrogate - 0xd800) << 10) +
                        (character - 0xdc00);
                pendingHighSurrogate = 0;
                writeCodePoint(codePoint);
                return;
            }
            pendingHighSurrogate = 0;
            out.write('?');
        }
        if (character >= 0xd800 && character <= 0xdbff) {
            pendingHighSurrogate = (char) character;
        } else if (character >= 0xdc00 && character <= 0xdfff) {
            out.write('?');
        } else {
            writeCodePoint(character);
        }
    }

    private void writeCodePoint(int codePoint) throws IOException {
        if (codePoint < 0x80) {
            out.write(codePoint);
        } else if (codePoint < 0x800) {
            out.write(0xc0 | (codePoint >> 6));
            out.write(0x80 | (codePoint & 0x3f));
        } else if (codePoint < 0x10000) {
            out.write(0xe0 | (codePoint >> 12));
            out.write(0x80 | ((codePoint >> 6) & 0x3f));
            out.write(0x80 | (codePoint & 0x3f));
        } else {
            out.write(0xf0 | (codePoint >> 18));
            out.write(0x80 | ((codePoint >> 12) & 0x3f));
            out.write(0x80 | ((codePoint >> 6) & 0x3f));
            out.write(0x80 | (codePoint & 0x3f));
        }
    }

    private void encodeUtf16(int character) throws IOException {
        boolean little = encoding.equals("UTF-16LE");
        if (encoding.equals("UTF-16") && !started) {
            out.write(0xfe);
            out.write(0xff);
        }
        started = true;
        if (little) {
            out.write(character & 0xff);
            out.write((character >> 8) & 0xff);
        } else {
            out.write((character >> 8) & 0xff);
            out.write(character & 0xff);
        }
    }

    @Override
    public Writer append(CharSequence value) throws IOException {
        String text = value == null ? "null" : value.toString();
        write(text, 0, text.length());
        return this;
    }

    @Override
    public Writer append(
            CharSequence value, int start, int end)
            throws IOException {
        if (value == null) value = "null";
        if (start < 0 || end < start || end > value.length()) {
            throw new IndexOutOfBoundsException();
        }
        append(value.subSequence(start, end));
        return this;
    }

    @Override
    public Writer append(char value) throws IOException {
        write(value);
        return this;
    }

    public void write(CharBuffer source) throws IOException {
        if (source == null) throw new NullPointerException();
        while (source.hasRemaining()) {
            char[] buffer = new char[Math.min(source.remaining(), 1024)];
            int count = buffer.length;
            source.get(buffer, 0, count);
            write(buffer, 0, count);
        }
    }

    @Override
    public void flush() throws IOException {
        synchronized (lock) {
            ensureOpen();
            if (pendingHighSurrogate != 0) {
                pendingHighSurrogate = 0;
                out.write('?');
            }
            out.flush();
        }
    }

    @Override
    public void close() throws IOException {
        synchronized (lock) {
            if (closed) return;
            try {
                flush();
            } finally {
                closed = true;
                out.close();
            }
        }
    }

    // ---- overlay：flushBuffer（OpenJDK 内部面）----
    void flushBuffer() throws java.io.IOException {
        flush();
    }

}
