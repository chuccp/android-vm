package sun.nio.cs;

import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;

/** UTF-16BE：固定大端，无 BOM 处理。 */
public final class UTF_16BE extends HistoricallyNamedCharset {

    public static final UTF_16BE INSTANCE = new UTF_16BE();

    public UTF_16BE() {
        super("UTF-16BE", new String[] { "UTF_16BE", "ISO-10646-UCS-2", "X-UTF-16BE",
                "UnicodeBigUnmarked" });
    }

    @Override
    public String historicalName() {
        return "UTF-16BE";
    }

    @Override
    public boolean contains(Charset cs) {
        return cs instanceof UTF_16BE;
    }

    @Override
    public CharsetDecoder newDecoder() {
        return new UTF16Decoder(this, true, false);
    }

    @Override
    public CharsetEncoder newEncoder() {
        return new UTF16Encoder(this, true, false);
    }
}
