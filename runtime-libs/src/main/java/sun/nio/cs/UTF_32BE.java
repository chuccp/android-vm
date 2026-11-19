package sun.nio.cs;

import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;

/** UTF-32BE：固定 4 字节大端（OkHttp Util 常量面依赖）。 */
public final class UTF_32BE extends HistoricallyNamedCharset {

    public static final UTF_32BE INSTANCE = new UTF_32BE();

    public UTF_32BE() {
        super("UTF-32BE", new String[] { "UTF_32BE", "X-UTF-32BE" });
    }

    @Override
    public String historicalName() {
        return "UTF-32BE";
    }

    @Override
    public boolean contains(Charset cs) {
        return cs instanceof UTF_32BE || cs instanceof UTF_32LE;
    }

    @Override
    public CharsetDecoder newDecoder() {
        return new UTF32Decoder(this, true);
    }

    @Override
    public CharsetEncoder newEncoder() {
        return new UTF32Encoder(this, true);
    }
}
