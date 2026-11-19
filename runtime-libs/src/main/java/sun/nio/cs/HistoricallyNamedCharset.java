package sun.nio.cs;

import java.nio.charset.Charset;

/** JDK 历史名基类（StringCoding 等按 historicalName() 取编码名）。 */
public abstract class HistoricallyNamedCharset extends Charset {

    public HistoricallyNamedCharset(String canonicalName, String[] aliases) {
        super(canonicalName, aliases);
    }

    public String historicalName() {
        return name();
    }
}
