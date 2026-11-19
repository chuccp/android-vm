package java.sql;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：java.sql.Date 常用面
 * （Gson com.google.gson.internal.sql.SqlTypesSupport/SqlDateTypeAdapter
 * 消费面：Class.forName 探测 + (J) 构造 + valueOf/toString ISO 文本形）。
 * 语义对照 Android/OpenJDK java.sql.Date："yyyy-mm-dd" 文本形；底层
 * java.util.Date 为 vendored 裁剪面（getTime/setTime + 遗留 6 形构造）。
 */
public class Date extends java.util.Date {

    private static final long serialVersionUID = 7523967970034938905L;

    @Deprecated
    public Date(int year, int month, int day) {
        super(year, month, day, 0, 0, 0);
    }

    public Date(long date) {
        super(date);
    }

    /** "yyyy-mm-dd" 形解析（SimpleDateFormat 非容错口径；vendored Date.parse 同法）。 */
    public static Date valueOf(String s) {
        if (s == null) {
            throw new IllegalArgumentException("null");
        }
        try {
            java.text.SimpleDateFormat f =
                    new java.text.SimpleDateFormat("yyyy-MM-dd", java.util.Locale.US);
            f.setLenient(false);
            return new Date(f.parse(s).getTime());
        } catch (java.text.ParseException e) {
            throw new IllegalArgumentException("valueOf: " + s, e);
        }
    }

    @Override
    public String toString() {
        return new java.text.SimpleDateFormat("yyyy-MM-dd", java.util.Locale.US).format(this);
    }
}
