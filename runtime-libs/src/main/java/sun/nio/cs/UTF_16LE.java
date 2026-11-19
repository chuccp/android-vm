package sun.nio.cs;

import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;

/** UTF-16LE：固定小端，无 BOM 处理。 */
public final class UTF_16LE extends HistoricallyNamedCharset {

    public static final UTF_16LE INSTANCE = new UTF_16LE();

    public UTF_16LE() {
        super("UTF-16LE", new String[] { "UTF_16LE", "X-UTF-16LE", "UnicodeLittleUnmarked" });
    }

    @Override
    public String historicalName() {
        return "UTF-16LE";
    }

    @Override
    public boolean contains(Charset cs) {
        return cs instanceof UTF_16LE;
    }

    @Override
    public CharsetDecoder newDecoder() {
        return new UTF16Decoder(this, false, false);
    }

    @Override
    public CharsetEncoder newEncoder() {
        return new UTF16Encoder(this, false, false);
    }
}
