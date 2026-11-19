package java.net;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.UnsupportedEncodingException;
import java.nio.charset.Charset;
import java.util.Objects;

public class URLDecoder {
    private URLDecoder() {}

    public static String decode(String text, String charsetName)
            throws UnsupportedEncodingException {
        Objects.requireNonNull(charsetName);
        return decode(text, Charset.forName(charsetName));
    }

    public static String decode(String text, Charset charset) {
        Objects.requireNonNull(text);
        Objects.requireNonNull(charset);
        ByteArrayOutputStream bytes = new ByteArrayOutputStream(text.length());
        for (int index = 0; index < text.length(); index++) {
            char current = text.charAt(index);
            if (current == '+') {
                bytes.write(' ');
            } else if (current == '%') {
                if (index + 2 >= text.length()) {
                    throw new IllegalArgumentException(
                            "URLDecoder: Incomplete trailing escape (%) pattern");
                }
                int high = hexValue(text.charAt(++index));
                int low = hexValue(text.charAt(++index));
                if (high < 0 || low < 0) {
                    throw new IllegalArgumentException(
                            "URLDecoder: Illegal hex characters in escape (%) pattern");
                }
                bytes.write((high << 4) | low);
            } else {
                bytes.write(current);
            }
        }
        try {
            byte[] decoded = bytes.toByteArray();
            String encoding = charset.name().toUpperCase();
            if (encoding.equals("UTF-8") || encoding.equals("UTF8") ||
                    encoding.equals("US-ASCII") || encoding.equals("ASCII") ||
                    encoding.equals("ISO-8859-1") ||
                    encoding.equals("LATIN1") ||
                    encoding.equals("ISO8859-1")) {
                return new String(decoded, charset);
            }
            InputStreamReader reader = new InputStreamReader(
                    new ByteArrayInputStream(decoded), charset);
            StringBuilder result = new StringBuilder(text.length());
            char[] buffer = new char[256];
            int count;
            while ((count = reader.read(buffer, 0, buffer.length)) >= 0) {
                result.append(buffer, 0, count);
            }
            reader.close();
            return result.toString();
        } catch (IOException failure) {
            throw new IllegalArgumentException(failure);
        }
    }

    private static int hexValue(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }
}
