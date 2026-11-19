package java.sql;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：java.sql.Time 常用面
 * （Gson SqlTypesSupport/SqlTimeTypeAdapter 消费面：Class.forName 探测 +
 * (J) 构造 + valueOf/toString "hh:mm:ss" 文本形）。语义对照
 * Android/OpenJDK java.sql.Time；底层 java.util.Date 为 vendored 裁剪面。
 */
public class Time extends java.util.Date {

    private static final long serialVersionUID = 8397324470061671614L;

    @Deprecated
    public Time(int hour, int minute, int second) {
        super(70, 0, 1, hour, minute, second);
    }

    public Time(long time) {
        super(time);
    }

    /** "hh:mm:ss" 形解析（SimpleDateFormat 非容错口径）。 */
    public static Time valueOf(String s) {
        if (s == null) {
            throw new IllegalArgumentException("null");
        }
        try {
            java.text.SimpleDateFormat f =
                    new java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.US);
            f.setLenient(false);
            return new Time(f.parse(s).getTime());
        } catch (java.text.ParseException e) {
            throw new IllegalArgumentException("valueOf: " + s, e);
        }
    }

    @Override
    public String toString() {
        return new java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.US).format(this);
    }
}
