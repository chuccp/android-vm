package java.util;

public class Calendar {
    public static final int ERA = 0;
    public static final int YEAR = 1;
    public static final int MONTH = 2;
    public static final int WEEK_OF_MONTH = 4;
    public static final int DATE = 5;
    public static final int DAY_OF_MONTH = DATE;
    public static final int DAY_OF_YEAR = 6;
    public static final int DAY_OF_WEEK = 7;
    public static final int HOUR = 10;
    public static final int MINUTE = 12;
    public static final int SECOND = 13;
    public static final int MILLISECOND = 14;
    public static final int AM = 0;
    public static final int PM = 1;

    protected long millis;
    protected boolean lenient = true;
    protected TimeZone timeZone = TimeZone.getDefault();

    protected Calendar() {
        millis = System.currentTimeMillis();
    }

    public static Calendar getInstance() {
        return new GregorianCalendar();
    }

    public static Calendar getInstance(TimeZone zone) {
        Calendar calendar = new GregorianCalendar();
        calendar.setTimeZone(zone);
        return calendar;
    }

    public static Calendar getInstance(Locale locale) {
        return new GregorianCalendar();
    }

    public int get(int field) {
        int[] values = fields();
        switch (field) {
            case ERA: return 1;
            case YEAR: return values[0];
            case MONTH: return values[1] - 1;
            case WEEK_OF_YEAR:
            case WEEK_OF_MONTH: return 0;
            case DATE: return values[2];
            case DAY_OF_YEAR: return dayOfYear(values[0], values[1], values[2]);
            case DAY_OF_WEEK: return dayOfWeek();
            case HOUR: return values[3] % 12;
            case HOUR_OF_DAY: return values[3];
            case MINUTE: return values[4];
            case SECOND: return values[5];
            case MILLISECOND: return values[6];
            default: throw new IllegalArgumentException("Unknown field: " + field);
        }
    }

    public void set(int field, int value) {
        int[] values = fields();
        switch (field) {
            case YEAR: values[0] = value; break;
            case MONTH: values[1] = value + 1; break;
            case DATE: values[2] = value; break;
            case HOUR: values[3] = value % 12; break;
            case HOUR_OF_DAY: values[3] = value; break;
            case MINUTE: values[4] = value; break;
            case SECOND: values[5] = value; break;
            case MILLISECOND: values[6] = value; break;
            default: throw new IllegalArgumentException("Unknown field: " + field);
        }
        assign(values);
    }

    public final void set(int year, int month, int date) {
        set(year, month, date, 0, 0, 0);
    }

    public final void set(int year, int month, int date,
            int hourOfDay, int minute) {
        set(year, month, date, hourOfDay, minute, 0);
    }

    public final void set(int year, int month, int date,
            int hourOfDay, int minute, int second) {
        int[] values = {year, month + 1, date, hourOfDay, minute, second, 0};
        assign(values);
    }

    public final void clear() {
        millis = 0;
    }

    public final void clear(int field) {
        set(field, 0);
    }

    public Date getTime() {
        return new Date(millis);
    }

    public void setTime(Date date) {
        millis = date.getTime();
    }

    public long getTimeInMillis() {
        return millis;
    }

    public void setTimeInMillis(long millis) {
        this.millis = millis;
    }

    public TimeZone getTimeZone() {
        return timeZone;
    }

    public void setTimeZone(TimeZone zone) {
        timeZone = zone;
    }

    public boolean isLenient() {
        return lenient;
    }

    public void setLenient(boolean lenient) {
        this.lenient = lenient;
    }

    public void add(int field, int amount) {
        int[] values = fields();
        if (field == YEAR) {
            values[0] += amount;
        } else if (field == MONTH) {
            long total = (long) values[0] * 12L + values[1] - 1 + amount;
            values[0] = (int) floorDiv(total, 12);
            values[1] = (int) floorMod(total, 12) + 1;
            values[2] = Math.min(values[2], lengthOfMonth(values[0], values[1]));
        } else {
            long day = field == DATE ? amount : field == HOUR_OF_DAY ? 0 : 0;
            long delta = field == DATE ? day * 86_400_000L :
                field == HOUR_OF_DAY ? amount * 3_600_000L :
                field == MINUTE ? amount * 60_000L :
                field == SECOND ? amount * 1000L :
                field == MILLISECOND ? amount : 0L;
            millis += delta;
            return;
        }
        assign(values);
    }

    public boolean before(Object other) {
        return other instanceof Calendar && millis < ((Calendar) other).millis;
    }

    public boolean after(Object other) {
        return other instanceof Calendar && millis > ((Calendar) other).millis;
    }

    public int compareTo(Calendar other) {
        return millis < other.millis ? -1 : millis > other.millis ? 1 : 0;
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof Calendar && ((Calendar) other).millis == millis;
    }

    @Override
    public int hashCode() {
        return (int) (millis ^ (millis >>> 32));
    }

    @Override
    public Object clone() {
        Calendar copy = new GregorianCalendar();
        copy.millis = millis;
        copy.lenient = lenient;
        copy.timeZone = (TimeZone) timeZone.clone();
        return copy;
    }

    private int[] fields() {
        long local = millis + timeZone.getOffset(millis);
        long epochDay = floorDiv(local, 86_400_000L);
        int[] date = dateFromEpochDay(epochDay);
        int millisOfDay = (int) floorMod(local, 86_400_000L);
        return new int[] {
            date[0], date[1], date[2],
            millisOfDay / 3_600_000,
            millisOfDay / 60_000 % 60,
            millisOfDay / 1000 % 60,
            millisOfDay % 1000,
        };
    }

    private void assign(int[] values) {
        if (!lenient && (values[1] < 1 || values[1] > 12 ||
                values[2] < 1 ||
                values[2] > lengthOfMonth(values[0], values[1]))) {
            throw new IllegalArgumentException("Invalid calendar fields");
        }
        long local = toEpochDay(values[0], values[1], values[2]) *
            86_400_000L + values[3] * 3_600_000L +
            values[4] * 60_000L + values[5] * 1000L + values[6];
        millis = local - timeZone.getOffset(local - timeZone.getRawOffset());
    }

    private int dayOfWeek() {
        return (int) floorMod(floorDiv(
            millis + timeZone.getOffset(millis), 86_400_000L) + 5, 7);
    }

    private static int dayOfYear(int year, int month, int day) {
        int result = day;
        for (int current = 1; current < month; current++) {
            result += lengthOfMonth(year, current);
        }
        return result;
    }

    private static int[] dateFromEpochDay(long epochDay) {
        int low = -999_999_999;
        int high = 999_999_999;
        while (low < high) {
            int middle = low + (high - low + 1) / 2;
            if (toEpochDay(middle, 1, 1) <= epochDay) low = middle;
            else high = middle - 1;
        }
        int day = (int) (epochDay - toEpochDay(low, 1, 1)) + 1;
        int month = 1;
        while (day > lengthOfMonth(low, month)) {
            day -= lengthOfMonth(low, month++);
        }
        return new int[] {low, month, day};
    }

    private static int lengthOfMonth(int year, int month) {
        if (month == 2) return isLeapYear(year) ? 29 : 28;
        return DAYS_PER_MONTH[month - 1];
    }

    private static boolean isLeapYear(int year) {
        return (year & 3) == 0 &&
            (year % 100 != 0 || year % 400 == 0);
    }

    private static long toEpochDay(int year, int month, int day) {
        long y = year;
        long m = month;
        long total = 365L * y;
        if (y >= 0) {
            total += (y + 3) / 4 - (y + 99) / 100 + (y + 399) / 400;
        } else {
            total -= y / -4 - y / -100 + y / -400;
        }
        total += (367L * m - 362L) / 12L + day - 1;
        if (m > 2) total -= isLeapYear(year) ? 1 : 2;
        return total - 719_528L;
    }

    private static long floorDiv(long value, long divisor) {
        long result = value / divisor;
        if ((value ^ divisor) < 0 && result * divisor != value) result--;
        return result;
    }

    private static long floorMod(long value, long divisor) {
        return value - floorDiv(value, divisor) * divisor;
    }

    private static final int[] DAYS_PER_MONTH =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // ---- overlay：风格/周/极值 API 补全（供 MessageFormat/SimpleTimeZone/
    // GregorianCalendar 全量源编译）----
    public static final int SUNDAY = 1;
    public static final int MONDAY = 2;
    public static final int TUESDAY = 3;
    public static final int WEDNESDAY = 4;
    public static final int THURSDAY = 5;
    public static final int FRIDAY = 6;
    public static final int SATURDAY = 7;

    public static final int JANUARY = 0;
    public static final int FEBRUARY = 1;
    public static final int MARCH = 2;
    public static final int APRIL = 3;
    public static final int MAY = 4;
    public static final int JUNE = 5;
    public static final int JULY = 6;
    public static final int AUGUST = 7;
    public static final int SEPTEMBER = 8;
    public static final int OCTOBER = 9;
    public static final int NOVEMBER = 10;
    public static final int DECEMBER = 11;
    public static final int UNDECIMBER = 12;

    public static final int AM_PM = 9;
    public static final int HOUR_OF_DAY = 11;
    public static final int WEEK_OF_YEAR = 3;
    public static final int ZONE_OFFSET = 15;
    public static final int DST_OFFSET = 16;
    public static final int FIELD_COUNT = 17;
    public static final int ALL_STYLES = 0;
    public static final int SHORT_FORMAT = 2;
    public static final int LONG_FORMAT = 0;
    public static final int SHORT_STANDALONE = 3;
    public static final int LONG_STANDALONE = 1;
    public static final int NARROW_FORMAT = 4;
    public static final int NARROW_STANDALONE = 5;
    public static final int DAY_OF_WEEK_IN_MONTH = 8;
    public static final int ZONE_OFFSET_MASK = 1 << ZONE_OFFSET;
    public static final int DST_OFFSET_MASK = 1 << DST_OFFSET;

    protected int firstDayOfWeek = SUNDAY;
    protected int minimalDaysInFirstWeek = 1;

    protected int internalGet(int field) {
        return get(field);
    }

    protected void internalSet(int field, int value) {
        // minimal: subclasses override
    }

    protected boolean isFieldSet(int fieldMask, int field) {
        return (fieldMask & (1 << field)) != 0;
    }

    public int getFirstDayOfWeek() {
        return firstDayOfWeek;
    }

    public void setFirstDayOfWeek(int value) {
        firstDayOfWeek = value;
    }

    public int getMinimalDaysInFirstWeek() {
        return minimalDaysInFirstWeek;
    }

    public void setMinimalDaysInFirstWeek(int value) {
        minimalDaysInFirstWeek = value;
    }

    public int getMinimum(int field) {
        return 0;
    }

    public int getMaximum(int field) {
        return Integer.MAX_VALUE;
    }

    public int getGreatestMinimum(int field) {
        return getMinimum(field);
    }

    public int getLeastMaximum(int field) {
        return getMaximum(field);
    }

    public int getActualMinimum(int field) {
        return getMinimum(field);
    }

    public int getActualMaximum(int field) {
        return getMaximum(field);
    }

    public boolean isWeekDateSupported() {
        return false;
    }

    public void setWeekDate(int weekYear, int weekOfYear, int dayOfWeek) {
        throw new UnsupportedOperationException("weekDate");
    }

    public TimeZone getZone() {
        return timeZone;
    }
}
