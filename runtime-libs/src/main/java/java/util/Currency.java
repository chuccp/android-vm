package java.util;

import java.io.Serializable;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 货币（Android 公开面；§31.7 C 档 java.* → lib 形态）。
 *
 * <p>AOSP java.util.Currency 公开面收敛：ISO 4217 主表（码/数字码/小数位/符号）+
 * getInstance 静面 + getAvailableCurrencies + 符号/小数位查询。原 vendored 全量
 * OpenJDK 版依赖 jdk.internal/sun.util.locale.provider 与货币数据文件（构建期
 * uncompilable 剔除）；本实现自包含，常见币种全量、长尾码按 code 建实例（小数位
 * 2 兜底、符号=code）——需要全 ISO 表时按同法扩表。Locale→国家码映射经
 * Locale.getCountry()。
 */
public final class Currency implements Serializable {

    private static final long serialVersionUID = -158308464356906721L;

    /** code -> [numericCode, fractionDigits, symbol]（ISO 4217 主表抽样 + 常用扩展） */
    private static final Object[][] TABLE = {
        {"CNY", 156, 2, "¥"}, {"USD", 840, 2, "$"}, {"EUR", 978, 2, "€"},
        {"JPY", 392, 0, "¥"}, {"GBP", 826, 2, "£"}, {"AUD", 36, 2, "$"},
        {"CAD", 124, 2, "$"}, {"CHF", 756, 2, "CHF"}, {"HKD", 344, 2, "HK$"},
        {"SGD", 702, 2, "S$"}, {"KRW", 410, 0, "₩"}, {"INR", 356, 2, "₹"},
        {"RUB", 643, 2, "₽"}, {"THB", 764, 2, "฿"}, {"MYR", 458, 2, "RM"},
        {"IDR", 360, 2, "Rp"}, {"PHP", 608, 2, "₱"}, {"TWD", 900, 2, "NT$"},
        {"NZD", 554, 2, "$"}, {"SEK", 752, 2, "kr"}, {"NOK", 578, 2, "kr"},
        {"DKK", 208, 2, "kr"}, {"PLN", 985, 2, "zł"}, {"TRY", 949, 2, "₺"},
        {"BRL", 986, 2, "R$"}, {"MXN", 484, 2, "Mex$"}, {"ZAR", 710, 2, "R"},
        {"AED", 784, 2, "د.إ"}, {"SAR", 682, 2, "﷼"}, {"ILS", 376, 2, "₪"},
        {"ARS", 32, 2, "$"}, {"CLP", 152, 0, "$"}, {"COP", 170, 2, "$"},
        {"EGP", 818, 2, "E£"}, {"PKR", 586, 2, "₨"}, {"BDT", 50, 2, "৳"},
        {"VND", 704, 0, "₫"}, {"UAH", 980, 2, "₴"}, {"CZK", 203, 2, "Kč"},
        {"HUF", 348, 0, "Ft"}, {"RON", 946, 2, "lei"}, {"BGN", 975, 2, "лв"},
        {"ISK", 352, 0, "kr"}, {"NGN", 566, 2, "₦"}, {"KES", 404, 2, "KSh"},
    };

    /** 国家码 -> 货币码（Locale.getCountry() 映射；常用面） */
    private static final Object[][] COUNTRY = {
        {"CN", "CNY"}, {"US", "USD"}, {"JP", "JPY"}, {"GB", "GBP"},
        {"DE", "EUR"}, {"FR", "EUR"}, {"IT", "EUR"}, {"ES", "EUR"},
        {"NL", "EUR"}, {"BE", "EUR"}, {"AT", "EUR"}, {"PT", "EUR"},
        {"IE", "EUR"}, {"GR", "EUR"}, {"FI", "EUR"}, {"SK", "EUR"},
        {"SI", "EUR"}, {"LT", "EUR"}, {"LV", "EUR"}, {"EE", "EUR"},
        {"LU", "EUR"}, {"MT", "EUR"}, {"CY", "EUR"}, {"KR", "KRW"},
        {"IN", "INR"}, {"TW", "TWD"}, {"HK", "HKD"}, {"MO", "HKD"},
        {"SG", "SGD"}, {"AU", "AUD"}, {"NZ", "NZD"}, {"CA", "CAD"},
        {"CH", "CHF"}, {"LI", "CHF"}, {"RU", "RUB"}, {"TH", "THB"},
        {"MY", "MYR"}, {"ID", "IDR"}, {"PH", "PHP"}, {"SE", "SEK"},
        {"NO", "NOK"}, {"DK", "DKK"}, {"FO", "DKK"}, {"GL", "DKK"},
        {"PL", "PLN"}, {"TR", "TRY"}, {"BR", "BRL"}, {"MX", "MXN"},
        {"ZA", "ZAR"}, {"AE", "AED"}, {"SA", "SAR"}, {"IL", "ILS"},
        {"AR", "ARS"}, {"CL", "CLP"}, {"CO", "COP"}, {"EG", "EGP"},
        {"PK", "PKR"}, {"BD", "BDT"}, {"VN", "VND"}, {"UA", "UAH"},
        {"CZ", "CZK"}, {"HU", "HUF"}, {"RO", "RON"}, {"BG", "BGN"},
        {"IS", "ISK"}, {"NG", "NGN"}, {"KE", "KES"},
    };

    private static final ConcurrentHashMap<String, Currency> INSTANCES =
        new ConcurrentHashMap<>();

    private final String currencyCode;
    private final transient int defaultFractionDigits;
    private final transient int numericCode;
    private final transient String symbol;

    private Currency(String code, int numericCode, int fractionDigits, String symbol) {
        this.currencyCode = code;
        this.numericCode = numericCode;
        this.defaultFractionDigits = fractionDigits;
        this.symbol = symbol;
    }

    private static Currency create(String code) {
        for (Object[] row : TABLE) {
            if (row[0].equals(code)) {
                return new Currency(code, (Integer) row[1], (Integer) row[2], (String) row[3]);
            }
        }
        // 长尾 ISO 码：按 code 建实例（AOSP 数据表之外的合法码在扩展表命中；
        // 未知码兜底小数位 2、符号=code——够 jackson 转序列化面用）
        return new Currency(code, -1, 2, code);
    }

    public static Currency getInstance(String currencyCode) {
        if (currencyCode == null) {
            throw new NullPointerException("currencyCode == null");
        }
        Currency c = INSTANCES.get(currencyCode);
        if (c != null) {
            return c;
        }
        // AOSP：未知码抛 IllegalArgumentException（三字母大写为合法形状）
        boolean known = false;
        for (Object[] row : TABLE) {
            if (row[0].equals(currencyCode)) {
                known = true;
                break;
            }
        }
        if (!known && (currencyCode.length() != 3 || !isUpperAlpha(currencyCode))) {
            throw new IllegalArgumentException("code = " + currencyCode);
        }
        c = create(currencyCode);
        Currency prev = INSTANCES.putIfAbsent(currencyCode, c);
        return prev != null ? prev : c;
    }

    private static boolean isUpperAlpha(String s) {
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch < 'A' || ch > 'Z') return false;
        }
        return true;
    }

    public static Currency getInstance(Locale locale) {
        if (locale == null) {
            throw new NullPointerException("locale == null");
        }
        String country = locale.getCountry();
        if (country == null || country.length() == 0) {
            throw new IllegalArgumentException("Unsupported locale " + locale);
        }
        for (Object[] row : COUNTRY) {
            if (row[0].equals(country)) {
                return getInstance((String) row[1]);
            }
        }
        throw new IllegalArgumentException("Unsupported locale " + locale);
    }

    public static Set<Currency> getAvailableCurrencies() {
        HashSet<Currency> out = new HashSet<>(TABLE.length);
        for (Object[] row : TABLE) {
            out.add(getInstance((String) row[0]));
        }
        return out;
    }

    public String getCurrencyCode() {
        return currencyCode;
    }

    public int getNumericCode() {
        return numericCode;
    }

    public int getDefaultFractionDigits() {
        return defaultFractionDigits;
    }

    public String getSymbol() {
        return symbol;
    }

    public String getSymbol(Locale locale) {
        // 符号不随 locale 变化（AOSP 数据面后置；常用场景 code 符号恒定）
        return symbol;
    }

    @Override
    public String toString() {
        return currencyCode;
    }

    @Override
    public boolean equals(Object obj) {
        return (obj instanceof Currency)
                && currencyCode.equals(((Currency) obj).currencyCode);
    }

    @Override
    public int hashCode() {
        return currencyCode.hashCode();
    }

    private Object readResolve() {
        return getInstance(currencyCode);
    }
}
