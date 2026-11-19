package java.text;

import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

public abstract class DateFormat extends java.text.Format {
    protected Locale locale;
    protected TimeZone timeZone = TimeZone.getDefault();

    public String format(Date date) {
        return format(
                date,
                new StringBuffer(),
                new FieldPosition(0)).toString();
    }

    @Override
    public StringBuffer format(Object obj, StringBuffer toAppendTo,
                               java.text.FieldPosition pos) {
        return format((java.util.Date) obj, toAppendTo, pos);
    }

    public abstract StringBuffer format(Date date, StringBuffer target,
        java.text.FieldPosition position);

    public abstract Date parse(String source) throws ParseException;

    // ---- overlay：时区面（jackson StdDateFormat 等经 DateFormat 静态类型引用；
    // 方法解析按声明类收口，子类 SimpleDateFormat 覆写同字段）----
    public void setTimeZone(TimeZone zone) {
        timeZone = zone;
    }

    public TimeZone getTimeZone() {
        return timeZone;
    }

    // ---- overlay：FULL/LONG/MEDIUM/SHORT/DEFAULT + getDateInstance/
    // getTimeInstance 静态工厂 API 补全（SimpleDateFormat 实现）----
    public static final int FULL = 0;
    public static final int LONG = 1;
    public static final int MEDIUM = 2;
    public static final int SHORT = 3;
    public static final int DEFAULT = MEDIUM;

    private static String patternFor(int style, boolean time, boolean date) {
        switch (style) {
            case FULL:
                return date ? "yyyy年M月d日EEEE" : "H时mm分ss秒 z";
            case LONG:
                return date ? "yyyy年M月d日" : "H:mm:ss";
            case SHORT:
                return date ? "y-M-d" : "H:mm";
            case MEDIUM:
            default:
                return date ? "yyyy-M-d" : "H:mm:ss";
        }
    }

    public static DateFormat getDateInstance(int style) {
        return getDateInstance(style, Locale.getDefault());
    }

    public static DateFormat getDateInstance(int style, Locale locale) {
        return new SimpleDateFormat(patternFor(style, true, true), locale);
    }

    public static DateFormat getTimeInstance(int style) {
        return getTimeInstance(style, Locale.getDefault());
    }

    public static DateFormat getTimeInstance(int style, Locale locale) {
        return new SimpleDateFormat(patternFor(style, true, false), locale);
    }

    public static DateFormat getDateTimeInstance(int dateStyle, int timeStyle) {
        return getDateTimeInstance(dateStyle, timeStyle, Locale.getDefault());
    }

    public static DateFormat getDateTimeInstance(int dateStyle, int timeStyle,
                                                 Locale locale) {
        return new SimpleDateFormat(
            patternFor(dateStyle, true, true) + " " + patternFor(timeStyle, true, false),
            locale);
    }

    public static DateFormat getInstance() {
        return getDateTimeInstance(SHORT, SHORT);
    }
}
