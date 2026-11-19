package sun.nio.cs;

import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;

/** UTF-32LE：固定 4 字节小端（OkHttp Util 常量面依赖）。 */
public final class UTF_32LE extends HistoricallyNamedCharset {

    public static final UTF_32LE INSTANCE = new UTF_32LE();

    public UTF_32LE() {
        super("UTF-32LE", new String[] { "UTF_32LE", "X-UTF-32LE" });
    }

    @Override
    public String historicalName() {
        return "UTF-32LE";
    }

    @Override
    public boolean contains(Charset cs) {
        return cs instanceof UTF_32BE || cs instanceof UTF_32LE;
    }

    @Override
    public CharsetDecoder newDecoder() {
        return new UTF32Decoder(this, false);
    }

    @Override
    public CharsetEncoder newEncoder() {
        return new UTF32Encoder(this, false);
    }
}
