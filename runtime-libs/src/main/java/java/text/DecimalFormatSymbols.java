package java.text;

import java.io.IOException;
import java.io.ObjectInputStream;
import java.io.Serializable;
import java.util.Currency;
import java.util.Locale;

public class DecimalFormatSymbols implements Cloneable, Serializable {

    public DecimalFormatSymbols() {
        initialize( Locale.getDefault(Locale.Category.FORMAT) );
    }

    public DecimalFormatSymbols( Locale locale ) {
        initialize( locale );
    }

    public static Locale[] getAvailableLocales() {
        // VM 无 CLDR/provider 数据面：仅声明固定符号表对应的 locale 集。
        return new Locale[] { Locale.US };
    }

    public static final DecimalFormatSymbols getInstance() {
        return getInstance(Locale.getDefault(Locale.Category.FORMAT));
    }

    public static final DecimalFormatSymbols getInstance(Locale locale) {
        return new DecimalFormatSymbols(locale);
    }

    public char getZeroDigit() {
        return zeroDigit;
    }

    public void setZeroDigit(char zeroDigit) {
        this.zeroDigit = zeroDigit;
    }

    public char getGroupingSeparator() {
        return groupingSeparator;
    }

    public void setGroupingSeparator(char groupingSeparator) {
        this.groupingSeparator = groupingSeparator;
    }

    public char getDecimalSeparator() {
        return decimalSeparator;
    }

    public void setDecimalSeparator(char decimalSeparator) {
        this.decimalSeparator = decimalSeparator;
    }

    public char getPerMill() {
        return perMill;
    }

    public void setPerMill(char perMill) {
        this.perMill = perMill;
    }

    public char getPercent() {
        return percent;
    }

    public void setPercent(char percent) {
        this.percent = percent;
    }

    public char getDigit() {
        return digit;
    }

    public void setDigit(char digit) {
        this.digit = digit;
    }

    public char getPatternSeparator() {
        return patternSeparator;
    }

    public void setPatternSeparator(char patternSeparator) {
        this.patternSeparator = patternSeparator;
    }

    public String getInfinity() {
        return infinity;
    }

    public void setInfinity(String infinity) {
        this.infinity = infinity;
    }

    public String getNaN() {
        return NaN;
    }

    public void setNaN(String NaN) {
        this.NaN = NaN;
    }

    public char getMinusSign() {
        return minusSign;
    }

    public void setMinusSign(char minusSign) {
        this.minusSign = minusSign;
    }

    public String getCurrencySymbol()
    {
        initializeCurrency(locale);
        return currencySymbol;
    }

    public void setCurrencySymbol(String currency)
    {
        initializeCurrency(locale);
        currencySymbol = currency;
    }

    public String getInternationalCurrencySymbol()
    {
        initializeCurrency(locale);
        return intlCurrencySymbol;
    }

    public void setInternationalCurrencySymbol(String currencyCode)
    {
        initializeCurrency(locale);
        intlCurrencySymbol = currencyCode;
        currency = null;
        if (currencyCode != null) {
            try {
                currency = Currency.getInstance(currencyCode);
                currencySymbol = currency.getSymbol();
            } catch (IllegalArgumentException e) {
            }
        }
    }

    public Currency getCurrency() {
        initializeCurrency(locale);
        return currency;
    }

    public void setCurrency(Currency currency) {
        if (currency == null) {
            throw new NullPointerException();
        }
        initializeCurrency(locale);
        this.currency = currency;
        intlCurrencySymbol = currency.getCurrencyCode();
        currencySymbol = currency.getSymbol(locale);
    }

    public char getMonetaryDecimalSeparator()
    {
        return monetarySeparator;
    }

    public void setMonetaryDecimalSeparator(char sep)
    {
        monetarySeparator = sep;
    }

    char getExponentialSymbol()
    {
        return exponential;
    }

    public String getExponentSeparator()
    {
        return exponentialSeparator;
    }

    void setExponentialSymbol(char exp)
    {
        exponential = exp;
    }

    public void setExponentSeparator(String exp)
    {
        if (exp == null) {
            throw new NullPointerException();
        }
        exponentialSeparator = exp;
    }

    @Override
    public Object clone() {
        try {
            return (DecimalFormatSymbols)super.clone();

        } catch (CloneNotSupportedException e) {
            throw new InternalError(e);
        }
    }

    @Override
    public boolean equals(Object obj) {
        if (obj == null) return false;
        if (this == obj) return true;
        if (getClass() != obj.getClass()) return false;
        DecimalFormatSymbols other = (DecimalFormatSymbols) obj;
        return (zeroDigit == other.zeroDigit &&
        groupingSeparator == other.groupingSeparator &&
        decimalSeparator == other.decimalSeparator &&
        percent == other.percent &&
        perMill == other.perMill &&
        digit == other.digit &&
        minusSign == other.minusSign &&
        patternSeparator == other.patternSeparator &&
        infinity.equals(other.infinity) &&
        NaN.equals(other.NaN) &&
        getCurrencySymbol().equals(other.getCurrencySymbol()) &&
        intlCurrencySymbol.equals(other.intlCurrencySymbol) &&
        currency == other.currency &&
        monetarySeparator == other.monetarySeparator &&
        exponentialSeparator.equals(other.exponentialSeparator) &&
        locale.equals(other.locale));
    }

    @Override
    public int hashCode() {
            int result = zeroDigit;
            result = result * 37 + groupingSeparator;
            result = result * 37 + decimalSeparator;
            return result;
    }

    private void initialize( Locale locale ) {
        // VM \u7B80\u5316\uFF1A\u65E0 CLDR/provider \u8D44\u6E90\u9762\uFF08sun.util.locale.provider.* \u672A\u968F
        // runtime-libs \u6253\u5305\uFF0CFormatter/Scanner \u94FE\u5728\u8FD0\u884C\u671F NoClassDefFoundError\uFF09\u3002
        // \u91C7\u7528 CLDR root \u7B26\u53F7\u96C6\uFF08ASCII \u6570\u5B57/\u5206\u9694\u7B26\uFF09\uFF0C\u8986\u76D6\u683C\u5F0F\u5316\u4E3B\u5E72\u3002
        this.locale = locale != null ? locale : Locale.ROOT;

        decimalSeparator = '.';
        groupingSeparator = ',';
        patternSeparator = ';';
        percent = '%';
        zeroDigit = '0';
        digit = '#';
        minusSign = '-';
        exponential = 'E';
        exponentialSeparator = "E";
        perMill = '\u2030';
        infinity = "\u221E";
        NaN = "NaN";

        intlCurrencySymbol = "XXX";
        currencySymbol = "\u00A4";

        monetarySeparator = decimalSeparator;
    }

    private void initializeCurrency(Locale locale) {
        if (currencyInitialized) {
            return;
        }

        if (locale != null && !locale.getCountry().isEmpty()) {
            try {
                currency = Currency.getInstance(locale);
            } catch (IllegalArgumentException e) {

            }
        }

        if (currency != null) {
            intlCurrencySymbol = currency.getCurrencyCode();
            currencySymbol = currency.getSymbol(locale);
        } else {
            intlCurrencySymbol = "XXX";
            try {
                currency = Currency.getInstance(intlCurrencySymbol);
            } catch (IllegalArgumentException e) {
            }
            currencySymbol = "\u00A4";
        }

        currencyInitialized = true;
    }

    private void readObject(ObjectInputStream stream)
            throws IOException, ClassNotFoundException {
        stream.defaultReadObject();
        if (serialVersionOnStream < 1) {

            monetarySeparator = decimalSeparator;
            exponential       = 'E';
        }
        if (serialVersionOnStream < 2) {

            locale = Locale.ROOT;
        }
        if (serialVersionOnStream < 3) {

            exponentialSeparator = Character.toString(exponential);
        }
        serialVersionOnStream = currentSerialVersion;

        if (intlCurrencySymbol != null) {
            try {
                 currency = Currency.getInstance(intlCurrencySymbol);
            } catch (IllegalArgumentException e) {
            }
            currencyInitialized = true;
        }
    }

    private  char    zeroDigit;

    private  char    groupingSeparator;

    private  char    decimalSeparator;

    private  char    perMill;

    private  char    percent;

    private  char    digit;

    private  char    patternSeparator;

    private  String  infinity;

    private  String  NaN;

    private  char    minusSign;

    private  String  currencySymbol;

    private  String  intlCurrencySymbol;

    private  char    monetarySeparator;

    private  char    exponential;

    private  String    exponentialSeparator;

    private Locale locale;

    private transient Currency currency;
    private transient volatile boolean currencyInitialized;

    static final long serialVersionUID = 5772796243397350300L;

    private static final int currentSerialVersion = 3;

    private int serialVersionOnStream = currentSerialVersion;
}
