package sun.nio.cs;

import java.nio.charset.Charset;
import java.nio.charset.spi.CharsetProvider;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * 标准 charset 提供者（Charset.<clinit> 的 standardProvider 落点）。
 * 注册 UTF-8 / US-ASCII / ISO-8859-1 / UTF-16 / UTF-16BE / UTF-16LE 及
 * 其标准别名；名称匹配大小写不敏感（JDK CharsetProvider 约定）。
 */
public class StandardCharsets extends CharsetProvider {

    private static final Map<String, Charset> BY_NAME = new HashMap<>();
    private static final Set<Charset> ALL = new LinkedHashSet<>();

    private static void put(Charset cs, String... names) {
        try {
            android.util.Log.i("DexVmCharset", "put " + (cs == null ? "NULL"
                    : cs.name()) + " first=" + (names.length > 0 ? names[0] : ""));
        } catch (Throwable ignored) {
        }
        ALL.add(cs);
        for (String name : names) {
            String key = name.toLowerCase(Locale.ROOT);
            if (!BY_NAME.containsKey(key)) {
                BY_NAME.put(key, cs);
            }
        }
    }

    static {
        try {
            android.util.Log.i("DexVmCharset", "precheck US_ASCII.INSTANCE="
                    + US_ASCII.INSTANCE);
            US_ASCII probe = new US_ASCII();
            android.util.Log.i("DexVmCharset", "probe ok name=" + probe.name()
                    + " aliasCount=" + probe.aliases().size());
        } catch (Throwable t) {
            android.util.Log.w("DexVmCharset", "probe FAIL " + t);
        }
        put(UTF_8.INSTANCE, "UTF-8", "UTF8", "unicode-1-1-utf-8");
        put(ISO_8859_1.INSTANCE, "ISO-8859-1", "ISO8859-1", "8859_1", "ISO_8859-1", "latin1",
                "l1", "iso-ir-100", "cp819", "ibm819", "819", "csISOLatin1");
        put(US_ASCII.INSTANCE, "US-ASCII", "ASCII", "ANSI_X3.4-1968", "ANSI_X3.4-1986",
                "ISO_646.irv:1991", "ISO646-US", "us", "IBM367", "cp367", "csASCII");
        put(UTF_16.INSTANCE, "UTF-16", "UTF_16", "unicode", "utf16");
        put(UTF_16BE.INSTANCE, "UTF-16BE", "UTF_16BE", "ISO-10646-UCS-2", "X-UTF-16BE",
                "UnicodeBigUnmarked");
        put(UTF_16LE.INSTANCE, "UTF-16LE", "UTF_16LE", "X-UTF-16LE", "UnicodeLittleUnmarked");
        put(UTF_32BE.INSTANCE, "UTF-32BE", "UTF_32BE", "X-UTF-32BE");
        put(UTF_32LE.INSTANCE, "UTF-32LE", "UTF_32LE", "X-UTF-32LE");
        try {
            android.util.Log.i("DexVmCharset", "registry size=" + BY_NAME.size()
                    + " us-ascii=" + BY_NAME.get("us-ascii"));
        } catch (Throwable ignored) {
        }
    }

    public StandardCharsets() {
    }

    @Override
    public Charset charsetForName(String charsetName) {
        if (charsetName == null) {
            return null;
        }
        Charset cs = BY_NAME.get(charsetName.toLowerCase(Locale.ROOT));
        if (cs == null) {
            try {
                android.util.Log.i("DexVmCharset", "miss '" + charsetName
                        + "' size=" + BY_NAME.size());
            } catch (Throwable ignored) {
            }
        }
        return cs;
    }

    @Override
    public Iterator<Charset> charsets() {
        List<Charset> copy = new ArrayList<>(ALL);
        return copy.iterator();
    }
}
