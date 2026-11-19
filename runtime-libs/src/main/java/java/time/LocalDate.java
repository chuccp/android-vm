package java.time;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.io.ObjectInput;
import java.io.ObjectOutput;
import java.time.chrono.ChronoLocalDate;
import java.time.chrono.ChronoLocalDateTime;
import java.time.chrono.IsoChronology;
import java.time.temporal.ChronoField;
import java.time.temporal.ChronoUnit;
import java.time.temporal.Temporal;
import java.time.temporal.TemporalAccessor;
import java.time.temporal.TemporalAdjuster;
import java.time.temporal.TemporalAmount;
import java.time.temporal.TemporalField;
import java.time.temporal.TemporalQuery;
import java.time.temporal.TemporalQueries;
import java.time.temporal.TemporalUnit;
import java.time.temporal.UnsupportedTemporalTypeException;
import java.time.temporal.ValueRange;

// overlay：trimmed LocalDate（year/month/day 核心已实现）补全 java.time.temporal
// Temporal/TemporalAdjuster + java.time.chrono.ChronoLocalDate 实现面（OpenJDK 11
// 语义），解 java.time/chrono/format/zone 全量源编译闭包。vendored src 不动。
public final class LocalDate
        implements Temporal, TemporalAdjuster, ChronoLocalDate,
                   Comparable<ChronoLocalDate> {
    private static final int MIN_YEAR = -999_999_999;
    private static final int MAX_YEAR = 999_999_999;
    private static final long DAYS_0000_TO_1970 = 719_528L;
    private static final int DAYS_PER_CYCLE = 146_097;
    private static final int[] DAYS_PER_MONTH =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    public static final LocalDate MIN =
        LocalDate.of(-999_999_999, 1, 1);
    public static final LocalDate MAX =
        LocalDate.of(999_999_999, 12, 31);

    private final int year;
    private final int month;
    private final int day;

    private LocalDate(int year, int month, int day) {
        this.year = year;
        this.month = month;
        this.day = day;
    }

    public static LocalDate now() {
        return now(Clock.systemDefaultZone());
    }

    public static LocalDate now(Clock clock) {
        return ofEpochDay(Math.floorDiv(clock.millis(), 86_400_000L));
    }

    public static LocalDate now(ZoneId zone) {
        return now(Clock.system(zone));
    }

    public static LocalDate of(int year, int month, int day) {
        checkYear(year);
        if (month < 1 || month > 12) {
            throw new DateTimeException("Invalid value for Month: " + month);
        }
        if (day < 1 || day > lengthOfMonth(year, month)) {
            throw new DateTimeException("Invalid value for Day: " + day);
        }
        return new LocalDate(year, month, day);
    }

    public static LocalDate of(int year, Month month, int day) {
        return of(year, month.getValue(), day);
    }

    public static LocalDate ofYearDay(int year, int dayOfYear) {
        checkYear(year);
        boolean leap = isLeapYear(year);
        if (dayOfYear < 1 || dayOfYear > (leap ? 366 : 365)) {
            throw new DateTimeException("Invalid value for DayOfYear: " + dayOfYear);
        }
        int month = 1;
        int remaining = dayOfYear;
        while (remaining > lengthOfMonth(year, month)) {
            remaining -= lengthOfMonth(year, month);
            month++;
        }
        return new LocalDate(year, month, remaining);
    }

    public static LocalDate ofEpochDay(long epochDay) {
        long zeroDay = epochDay + DAYS_0000_TO_1970;
        zeroDay -= 60;
        long adjust = 0;
        if (zeroDay < 0) {
            long adjustCycles = (zeroDay + 1) / DAYS_PER_CYCLE - 1;
            adjust = adjustCycles * 400;
            zeroDay += -adjustCycles * DAYS_PER_CYCLE;
        }
        long yearEst = (400 * zeroDay + 591) / DAYS_PER_CYCLE;
        long doyEst = zeroDay - (365 * yearEst + yearEst / 4 - yearEst / 100 + yearEst / 400);
        if (doyEst < 0) {
            yearEst--;
            doyEst = zeroDay - (365 * yearEst + yearEst / 4 - yearEst / 100 + yearEst / 400);
        }
        yearEst += adjust;
        int marchDoy0 = (int) doyEst;
        int marchMonth0 = (marchDoy0 * 5 + 2) / 153;
        int month = (marchMonth0 + 2) % 12 + 1;
        int dom = marchDoy0 - (marchMonth0 * 306 + 5) / 10 + 1;
        yearEst += marchMonth0 / 10;
        return new LocalDate((int) yearEst, month, dom);
    }

    public static LocalDate parse(CharSequence text) {
        int length = text.length();
        if (length != 10 || text.charAt(4) != '-' || text.charAt(7) != '-') {
            throw new DateTimeException("Invalid LocalDate: " + text);
        }
        int year = parseDigits(text, 0, 4);
        int month = parseDigits(text, 5, 2);
        int day = parseDigits(text, 8, 2);
        return of(year, month, day);
    }

    public static LocalDate from(TemporalAccessor temporal) {
        if (temporal instanceof LocalDate) {
            return (LocalDate) temporal;
        }
        if (temporal instanceof ChronoLocalDate) {
            return ofEpochDay(((ChronoLocalDate) temporal).toEpochDay());
        }
        try {
            int year = temporal.get(ChronoField.YEAR);
            int month = temporal.get(ChronoField.MONTH_OF_YEAR);
            int day = temporal.get(ChronoField.DAY_OF_MONTH);
            return of(year, month, day);
        } catch (DateTimeException ex) {
            throw new DateTimeException(
                "Unable to obtain LocalDate from TemporalAccessor: "
                    + temporal, ex);
        }
    }

    public int getYear() {
        return year;
    }

    public Month getMonth() {
        return Month.of(month);
    }

    public int getMonthValue() {
        return month;
    }

    public int getDayOfMonth() {
        return day;
    }

    public DayOfWeek getDayOfWeek() {
        int dow0 = (int) Math.floorMod(toEpochDay() + 3, 7);
        return DayOfWeek.of(dow0 + 1);
    }

    public int getDayOfYear() {
        return (int) (toEpochDay() - of(year, 1, 1).toEpochDay()) + 1;
    }

    @Override
    public int lengthOfMonth() {
        return lengthOfMonth(year, month);
    }

    public int lengthOfYear() {
        return isLeapYear() ? 366 : 365;
    }

    @Override
    public boolean isLeapYear() {
        return isLeapYear(year);
    }

    @Override
    public IsoChronology getChronology() {
        return IsoChronology.INSTANCE;
    }

    public LocalDate plusYears(long years) {
        if (years == 0) return this;
        long newYear = year + years;
        checkYear(newYear);
        return resolvePreviousValid((int) newYear, month, day);
    }

    public LocalDate plusMonths(long months) {
        if (months == 0) return this;
        long monthCount = year * 12L + (month - 1);
        long calcMonths = monthCount + months;
        int newYear = (int) Math.floorDiv(calcMonths, 12L);
        int newMonth = (int) Math.floorMod(calcMonths, 12L) + 1;
        checkYear(newYear);
        return resolvePreviousValid(newYear, newMonth, day);
    }

    public LocalDate plusWeeks(long weeks) {
        return plusDays(Math.multiplyExact(weeks, 7));
    }

    public LocalDate plusDays(long days) {
        if (days == 0) return this;
        return ofEpochDay(Math.addExact(toEpochDay(), days));
    }

    public LocalDate minusYears(long years) {
        return plusYears(-years);
    }

    public LocalDate minusMonths(long months) {
        return plusMonths(-months);
    }

    public LocalDate minusWeeks(long weeks) {
        return plusWeeks(-weeks);
    }

    public LocalDate minusDays(long days) {
        return plusDays(-days);
    }

    public LocalDate plus(Period period) {
        long totalMonths = period.toTotalMonths();
        LocalDate result = plusMonths(totalMonths);
        return result.plusDays(period.getDays());
    }

    public LocalDate minus(Period period) {
        long totalMonths = period.toTotalMonths();
        LocalDate result = minusMonths(totalMonths);
        return result.minusDays(period.getDays());
    }

    public LocalDate withYear(int year) {
        checkYear(year);
        return resolvePreviousValid(year, month, day);
    }

    public LocalDate withMonth(int month) {
        if (month < 1 || month > 12) {
            throw new DateTimeException("Invalid value for Month: " + month);
        }
        return resolvePreviousValid(year, month, day);
    }

    public LocalDate withDayOfMonth(int day) {
        return of(year, month, day);
    }

    public LocalDate withDayOfYear(int dayOfYear) {
        return ofYearDay(year, dayOfYear);
    }

    @Override
    public LocalDate with(TemporalAdjuster adjuster) {
        if (adjuster instanceof LocalDate) {
            return (LocalDate) adjuster;
        }
        return (LocalDate) adjuster.adjustInto(this);
    }

    @Override
    public LocalDate with(TemporalField field, long newValue) {
        if (field instanceof ChronoField) {
            ChronoField f = (ChronoField) field;
            f.checkValidValue(newValue);
            switch (f) {
                case DAY_OF_WEEK:
                    return plusDays(newValue - getDayOfWeek().getValue());
                case ALIGNED_DAY_OF_WEEK_IN_MONTH:
                    return plusDays(newValue - get(ChronoField.ALIGNED_DAY_OF_WEEK_IN_MONTH));
                case ALIGNED_DAY_OF_WEEK_IN_YEAR:
                    return plusDays(newValue - get(ChronoField.ALIGNED_DAY_OF_WEEK_IN_YEAR));
                case DAY_OF_MONTH:
                    return withDayOfMonth((int) newValue);
                case DAY_OF_YEAR:
                    return withDayOfYear((int) newValue);
                case EPOCH_DAY:
                    return ofEpochDay(newValue);
                case ALIGNED_WEEK_OF_MONTH:
                    return plusDays((newValue - get(ChronoField.ALIGNED_WEEK_OF_MONTH)) * 7L);
                case ALIGNED_WEEK_OF_YEAR:
                    return plusDays((newValue - get(ChronoField.ALIGNED_WEEK_OF_YEAR)) * 7L);
                case MONTH_OF_YEAR:
                    return withMonth((int) newValue);
                case PROLEPTIC_MONTH:
                    return plusMonths(newValue - getProlepticMonth());
                case YEAR_OF_ERA:
                    return withYear((int) (get(ChronoField.ERA) == 1 ? newValue : 1 - newValue));
                case YEAR:
                    return withYear((int) newValue);
                case ERA:
                    return (get(ChronoField.ERA) == newValue)
                        ? this : withYear(1 - year);
                default:
                    throw new UnsupportedTemporalTypeException(
                        "Unsupported field: " + field);
            }
        }
        return (LocalDate) field.adjustInto(this, newValue);
    }

    @Override
    public LocalDate plus(long amountToAdd, TemporalUnit unit) {
        if (unit instanceof ChronoUnit) {
            switch ((ChronoUnit) unit) {
                case DAYS: return plusDays(amountToAdd);
                case WEEKS: return plusWeeks(amountToAdd);
                case MONTHS: return plusMonths(amountToAdd);
                case YEARS: return plusYears(amountToAdd);
                case DECADES: return plusYears(Math.multiplyExact(amountToAdd, 10));
                case CENTURIES: return plusYears(Math.multiplyExact(amountToAdd, 100));
                case MILLENNIA: return plusYears(Math.multiplyExact(amountToAdd, 1000));
                case ERAS:
                    return with(ChronoField.ERA,
                        Math.addExact(getLong(ChronoField.ERA), amountToAdd));
                default:
                    throw new UnsupportedTemporalTypeException(
                        "Unsupported unit: " + unit);
            }
        }
        return (LocalDate) unit.addTo(this, amountToAdd);
    }

    @Override
    public LocalDate minus(long amountToSubtract, TemporalUnit unit) {
        return (amountToSubtract == Long.MIN_VALUE)
            ? plus(Long.MAX_VALUE, unit).plus(1, unit)
            : plus(-amountToSubtract, unit);
    }

    @Override
    public LocalDate minus(TemporalAmount amount) {
        return (LocalDate) amount.subtractFrom(this);
    }

    @Override
    @SuppressWarnings("unchecked")
    public <R> R query(TemporalQuery<R> query) {
        if (query == TemporalQueries.chronology()) {
            return (R) IsoChronology.INSTANCE;
        }
        if (query == TemporalQueries.localDate()) {
            return (R) this;
        }
        return ChronoLocalDate.super.query(query);
    }

    @Override
    public Temporal adjustInto(Temporal temporal) {
        return temporal.with(ChronoField.EPOCH_DAY, toEpochDay());
    }

    @Override
    public ValueRange range(TemporalField field) {
        if (field instanceof ChronoField) {
            if (isSupported(field)) {
                ChronoField f = (ChronoField) field;
                switch (f) {
                    case DAY_OF_MONTH:
                        return ValueRange.of(1, lengthOfMonth());
                    case DAY_OF_YEAR:
                        return ValueRange.of(1, lengthOfYear());
                    case ALIGNED_WEEK_OF_MONTH:
                        return ValueRange.of(1, month == 2 && !isLeapYear() ? 4 : 5);
                    case YEAR_OF_ERA:
                        return (year <= 0)
                            ? ValueRange.of(1, MAX_YEAR + 1)
                            : ValueRange.of(1, MAX_YEAR);
                    default:
                        return field.range();
                }
            }
            throw new UnsupportedTemporalTypeException(
                "Unsupported field: " + field);
        }
        return field.rangeRefinedBy(this);
    }

    @Override
    public int get(TemporalField field) {
        if (field instanceof ChronoField) {
            return (int) getLong(field);
        }
        return ChronoLocalDate.super.get(field);
    }

    @Override
    public long getLong(TemporalField field) {
        if (field instanceof ChronoField) {
            switch ((ChronoField) field) {
                case DAY_OF_WEEK:
                    return getDayOfWeek().getValue();
                case ALIGNED_DAY_OF_WEEK_IN_MONTH:
                    return ((day - 1) % 7) + 1;
                case ALIGNED_DAY_OF_WEEK_IN_YEAR:
                    return ((getDayOfYear() - 1) % 7) + 1;
                case DAY_OF_MONTH:
                    return day;
                case DAY_OF_YEAR:
                    return getDayOfYear();
                case EPOCH_DAY:
                    return toEpochDay();
                case ALIGNED_WEEK_OF_MONTH:
                    return ((day - 1) / 7) + 1;
                case ALIGNED_WEEK_OF_YEAR:
                    return ((getDayOfYear() - 1) / 7) + 1;
                case MONTH_OF_YEAR:
                    return month;
                case PROLEPTIC_MONTH:
                    return getProlepticMonth();
                case YEAR_OF_ERA:
                    return (year >= 1 ? year : 1 - year);
                case YEAR:
                    return year;
                case ERA:
                    return (year >= 1 ? 1 : 0);
                default:
                    throw new UnsupportedTemporalTypeException(
                        "Unsupported field: " + field);
            }
        }
        return field.getFrom(this);
    }

    private long getProlepticMonth() {
        return year * 12L + (month - 1);
    }

    @Override
    public long until(Temporal endExclusive, TemporalUnit unit) {
        LocalDate end = LocalDate.from(endExclusive);
        if (unit instanceof ChronoUnit) {
            switch ((ChronoUnit) unit) {
                case DAYS:
                    return daysUntil(end);
                case WEEKS:
                    return daysUntil(end) / 7;
                case MONTHS:
                    return monthsUntil(end);
                case YEARS:
                    return monthsUntil(end) / 12;
                case DECADES:
                    return monthsUntil(end) / 120;
                case CENTURIES:
                    return monthsUntil(end) / 1200;
                case MILLENNIA:
                    return monthsUntil(end) / 12000;
                case ERAS:
                    return end.getLong(ChronoField.ERA) - getLong(ChronoField.ERA);
                default:
                    throw new UnsupportedTemporalTypeException(
                        "Unsupported unit: " + unit);
            }
        }
        return unit.between(this, end);
    }

    @Override
    public java.time.Period until(ChronoLocalDate endDate) {
        LocalDate end = LocalDate.from(endDate);
        long totalMonths = monthsUntil(end);
        int days = (int) (end.toEpochDay() - plusMonths(totalMonths).toEpochDay());
        return java.time.Period.of(
            (int) (totalMonths / 12), (int) (totalMonths % 12), days);
    }

    public long daysUntil(LocalDate endExclusive) {
        return endExclusive.toEpochDay() - toEpochDay();
    }

    private long monthsUntil(LocalDate end) {
        long packed1 = getProlepticMonth() * 32L + getDayOfMonth();
        long packed2 = end.getProlepticMonth() * 32L + end.getDayOfMonth();
        return (packed2 - packed1) / 32;
    }

    @Override
    public LocalDateTime atTime(LocalTime localTime) {
        return LocalDateTime.of(this, localTime);
    }

    public int compareTo0(LocalDate other) {
        return compareTo(other);
    }

    @Override
    public int compareTo(ChronoLocalDate other) {
        if (other instanceof LocalDate) {
            return compareTo((LocalDate) other);
        }
        return Long.compare(toEpochDay(), other.toEpochDay());
    }

    public long toEpochDay() {
        return toEpochDay(year, month, day);
    }

    public boolean isBefore(LocalDate other) {
        return compareTo(other) < 0;
    }

    public boolean isAfter(LocalDate other) {
        return compareTo(other) > 0;
    }

    public boolean isEqual(LocalDate other) {
        return compareTo(other) == 0;
    }

    public int compareTo(LocalDate other) {
        int yearCompare = year - other.year;
        if (yearCompare != 0) return yearCompare;
        int monthCompare = month - other.month;
        if (monthCompare != 0) return monthCompare;
        return day - other.day;
    }

    public void writeExternal(DataOutput out) throws IOException {
        out.writeInt(year);
        out.writeByte(month);
        out.writeByte(day);
    }

    public void writeExternal(ObjectOutput out) throws IOException {
        out.writeInt(year);
        out.writeByte(month);
        out.writeByte(day);
    }

    public static LocalDate readExternal(DataInput in) throws IOException {
        int year = in.readInt();
        int month = in.readByte();
        int day = in.readByte();
        return of(year, month, day);
    }

    public static LocalDate readExternal(ObjectInput in) throws IOException {
        int year = in.readInt();
        int month = in.readByte();
        int day = in.readByte();
        return of(year, month, day);
    }

    @Override
    public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof LocalDate)) return false;
        LocalDate date = (LocalDate) other;
        return year == date.year && month == date.month && day == date.day;
    }

    @Override
    public int hashCode() {
        return (year & 0xfffff800) ^
            ((year << 11) + (month << 6) + day);
    }

    @Override
    public String toString() {
        StringBuilder output = new StringBuilder(10);
        if (year < 0) {
            output.append('-');
        }
        int absoluteYear = year < 0 ? -year : year;
        if (absoluteYear < 1000) {
            output.append('0');
        }
        if (absoluteYear < 100) {
            output.append('0');
        }
        if (absoluteYear < 10) {
            output.append('0');
        }
        output.append(absoluteYear);
        output.append('-');
        if (month < 10) output.append('0');
        output.append(month);
        output.append('-');
        if (day < 10) output.append('0');
        output.append(day);
        return output.toString();
    }

    private static LocalDate resolvePreviousValid(
            int year, int month, int day) {
        int length = lengthOfMonth(year, month);
        return new LocalDate(year, month, Math.min(day, length));
    }

    private static int lengthOfMonth(int year, int month) {
        if (month == 2) return isLeapYear(year) ? 29 : 28;
        return DAYS_PER_MONTH[month - 1];
    }

    private static boolean isLeapYear(long year) {
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
        total += (367L * m - 362L) / 12L;
        total += day - 1;
        if (m > 2) {
            total -= isLeapYear(y) ? 1 : 2;
        }
        return total - DAYS_0000_TO_1970;
    }

    private static int parseDigits(CharSequence text, int offset, int count) {
        int value = 0;
        for (int index = 0; index < count; index++) {
            char character = text.charAt(offset + index);
            if (character < '0' || character > '9') {
                throw new DateTimeException("Invalid digit");
            }
            value = value * 10 + character - '0';
        }
        return value;
    }

    private static void checkYear(long year) {
        if (year < MIN_YEAR || year > MAX_YEAR) {
            throw new DateTimeException(
                "Invalid value for Year: " + year);
        }
    }
}
