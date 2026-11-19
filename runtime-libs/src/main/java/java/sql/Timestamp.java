package java.sql;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：java.sql.Timestamp 常用面
 * （Gson SqlTypesSupport/SqlTimestampTypeAdapter 消费面：Class.forName 探测 +
 * (J) 构造 + getTime/toString 纳秒文本形）。语义对照 Android/OpenJDK
 * java.sql.Timestamp：毫秒 + 纳秒（0..999,999,999），"yyyy-mm-dd hh:mm:ss
 * [.fffffffff]" 文本形；底层 java.util.Date 为 vendored 裁剪面。
 */
public class Timestamp extends java.util.Date {

    private static final long serialVersionUID = 2745119022149567504L;

    private int nanos;

    public Timestamp(long time) {
        super(time / 1000 * 1000);
        int millis = (int) (time % 1000);
        if (millis < 0) {
            millis += 1000;
            super.setTime(super.getTime() - 1000);
        }
        nanos = millis * 1000000;
    }

    @Deprecated
    public Timestamp(int year, int month, int date, int hour, int minute, int second, int nano) {
        super(year, month, date, hour, minute, second);
        if (nano < 0 || nano > 999999999) {
            throw new IllegalArgumentException("nanos " + nano);
        }
        nanos = nano;
    }

    /** 纳秒字段（0..999,999,999；OpenJDK 同义）。 */
    public int getNanos() {
        return nanos;
    }

    public void setNanos(int n) {
        if (n < 0 || n > 999999999) {
            throw new IllegalArgumentException("nanos " + n);
        }
        nanos = n;
    }

    /** 毫秒时间（super 秒对齐毫秒 + 纳秒截断毫秒；OpenJDK 同式）。 */
    @Override
    public long getTime() {
        return super.getTime() + (nanos / 1000000);
    }

    @Override
    public void setTime(long time) {
        super.setTime(time / 1000 * 1000);
        int millis = (int) (time % 1000);
        if (millis < 0) {
            millis += 1000;
            super.setTime(super.getTime() - 1000);
        }
        nanos = millis * 1000000;
    }

    /** "yyyy-mm-dd hh:mm:ss[.fffffffff]" 形解析（OpenJDK 同形）。 */
    public static Timestamp valueOf(String s) {
        if (s == null) {
            throw new IllegalArgumentException("null");
        }
        String text = s.trim();
        String fraction = "";
        int dot = text.indexOf('.');
        if (dot >= 0) {
            fraction = text.substring(dot + 1);
            text = text.substring(0, dot);
        }
        long base;
        try {
            java.text.SimpleDateFormat f =
                    new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US);
            f.setLenient(false);
            base = f.parse(text).getTime();
        } catch (java.text.ParseException e) {
            throw new IllegalArgumentException("valueOf: " + s, e);
        }
        int nanos = 0;
        if (!fraction.isEmpty()) {
            if (fraction.length() > 9 || fraction.charAt(0) < '0'
                    || fraction.charAt(0) > '9') {
                throw new IllegalArgumentException("valueOf: " + s);
            }
            StringBuilder padded = new StringBuilder(fraction);
            while (padded.length() < 9) padded.append('0');
            for (int i = 0; i < 9; i++) {
                char c = padded.charAt(i);
                if (c < '0' || c > '9') {
                    throw new IllegalArgumentException("valueOf: " + s);
                }
            }
            nanos = Integer.parseInt(padded.substring(0, 9));
        }
        Timestamp t = new Timestamp(base);
        t.nanos = nanos;
        return t;
    }

    @Override
    public String toString() {
        String base = new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US)
                .format(this);
        if (nanos == 0) {
            return base;
        }
        StringBuilder sb = new StringBuilder(base).append('.');
        String n = Integer.toString(nanos + 1000000000).substring(1);  // 9 位补零
        sb.append(n);
        int end = sb.length();
        while (end > 1 && sb.charAt(end - 1) == '0') end--;
        sb.setLength(end);
        return sb.toString();
    }
}
