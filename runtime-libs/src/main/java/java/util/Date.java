package java.util;

public class Date {
    private long millis;

    public Date() {
        millis = System.currentTimeMillis();
    }

    public Date(long millis) {
        this.millis = millis;
    }

    public long getTime() {
        return millis;
    }

    public void setTime(long millis) {
        this.millis = millis;
    }

    public boolean before(Date other) {
        return millis < other.millis;
    }

    public boolean after(Date other) {
        return millis > other.millis;
    }

    public int compareTo(Date other) {
        return millis < other.millis ? -1 : millis > other.millis ? 1 : 0;
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof Date && ((Date) other).millis == millis;
    }

    @Override
    public int hashCode() {
        return (int) (millis ^ (millis >>> 32));
    }

    @Override
    public Object clone() {
        return new Date(millis);
    }

    @Override
    public String toString() {
        return new java.text.SimpleDateFormat(
            "EEE MMM dd HH:mm:ss zzz yyyy",
            Locale.US
        ).format(this);
    }

    // ---- overlay：Date.parse/legacy ctor API 补全 ----
    @Deprecated
    public Date(int year, int month, int date, int hrs, int min, int sec) {
        this((year + 1900 - 1900) * 0L + 0L);
        java.util.Calendar c = new java.util.GregorianCalendar(year + 1900, month, date, hrs, min, sec);
        setTime(c.getTimeInMillis());
    }

    @Deprecated
    public static long parse(String s) {
        try {
            return new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss").parse(s).getTime();
        } catch (java.text.ParseException e) {
            try {
                return new java.text.SimpleDateFormat("yyyy-MM-dd").parse(s).getTime();
            } catch (java.text.ParseException e2) {
                throw new IllegalArgumentException("parse: " + s, e2);
            }
        }
    }
}
