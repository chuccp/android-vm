package sun.nio.cs;

import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;

/** UTF-16：默认大端，BOM（FE FF / FF FE）可选且决定字节序。 */
public final class UTF_16 extends HistoricallyNamedCharset {

    public static final UTF_16 INSTANCE = new UTF_16();

    public UTF_16() {
        super("UTF-16", new String[] { "UTF_16", "unicode", "utf16" });
    }

    @Override
    public String historicalName() {
        return "UTF-16";
    }

    @Override
    public boolean contains(Charset cs) {
        return cs instanceof UTF_16 || cs instanceof UTF_16LE || cs instanceof UTF_16BE;
    }

    @Override
    public CharsetDecoder newDecoder() {
        return new UTF16Decoder(this, true, true);
    }

    @Override
    public CharsetEncoder newEncoder() {
        return new UTF16Encoder(this, true, true);
    }
}
