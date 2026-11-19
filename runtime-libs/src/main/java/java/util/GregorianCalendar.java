package java.util;

public class GregorianCalendar extends Calendar {
    public GregorianCalendar() {
        super();
    }

    public GregorianCalendar(TimeZone zone) {
        super();
        setTimeZone(zone);
    }

    public GregorianCalendar(Locale locale) {
        super();
    }

    public GregorianCalendar(TimeZone zone, Locale locale) {
        super();
        setTimeZone(zone);
    }

    public GregorianCalendar(int year, int month, int dayOfMonth) {
        super();
        set(year, month, dayOfMonth);
    }

    public GregorianCalendar(int year, int month, int dayOfMonth,
            int hourOfDay, int minute) {
        super();
        set(year, month, dayOfMonth, hourOfDay, minute);
    }

    public GregorianCalendar(int year, int month, int dayOfMonth,
            int hourOfDay, int minute, int second) {
        super();
        set(year, month, dayOfMonth, hourOfDay, minute, second);
    }

    // ---- overlay：AD/BC/DEFAULT_GREGORIAN_CUTOVER/MONTH_LENGTH API 补全 ----
    public static final int BC = 0;
    public static final int AD = 1;
    public static final int BCE = 0;
    public static final int CE = 1;

    public static final Date DEFAULT_GREGORIAN_CUTOVER = new Date(Long.MIN_VALUE);

    public static final int[][] MONTH_LENGTH =
        {{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
         {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};

    public static final int[][] LEAP_MONTH_LENGTH =
        {{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
         {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};
}
