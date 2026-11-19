package java.text;

import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

public class SimpleDateFormat extends DateFormat {
    private String pattern;
    private boolean lenient = true;

    public SimpleDateFormat() {
        this("M/d/yy, h:mm a", Locale.getDefault());
    }

    public SimpleDateFormat(String pattern) {
        this(pattern, Locale.getDefault());
    }

    public SimpleDateFormat(String pattern, Locale locale) {
        if (pattern == null) throw new NullPointerException("pattern");
        this.pattern = pattern;
        this.locale = locale == null ? Locale.getDefault() : locale;
    }

    public void applyPattern(String pattern) {
        if (pattern == null) throw new NullPointerException("pattern");
        this.pattern = pattern;
    }

    public String toPattern() {
        return pattern;
    }

    public String toLocalizedPattern() {
        return pattern;
    }

    public void setLenient(boolean lenient) {
        this.lenient = lenient;
    }

    public boolean isLenient() {
        return lenient;
    }

    public void setTimeZone(TimeZone timeZone) {
        this.timeZone = timeZone;
    }

    public TimeZone getTimeZone() {
        return timeZone;
    }

    public final String format(Date date) {
        return format(date, new StringBuffer(), new FieldPosition(0)).toString();
    }

    public StringBuffer format(Object value, StringBuffer target,
            FieldPosition position) {
        if (value instanceof Date) {
            return format((Date) value, target, position);
        }
        if (value instanceof Number) {
            return format(
                    new Date(((Number) value).longValue()),
                    target,
                    position);
        }
        throw new IllegalArgumentException("Cannot format given Object");
    }

    @Override
    public StringBuffer format(Date date, StringBuffer target,
            FieldPosition position) {
        target.append(formatPattern(date.getTime()));
        return target;
    }

    @Override
    public Date parse(String source) throws ParseException {
        return new Date(parsePattern(source));
    }

    @Override
    public Object clone() {
        SimpleDateFormat copy = new SimpleDateFormat(pattern, locale);
        copy.timeZone = timeZone;
        copy.lenient = lenient;
        return copy;
    }

    private String formatPattern(long millis) {
        long localMillis = millis + timeZone.getOffset(millis);
        long epochDay = floorDiv(localMillis, 86_400_000L);
        int[] date = dateFromEpochDay(epochDay);
        int millisOfDay = (int) floorMod(localMillis, 86_400_000L);
        int hour = millisOfDay / 3_600_000;
        int minute = millisOfDay / 60_000 % 60;
        int second = millisOfDay / 1000 % 60;
        int millisecond = millisOfDay % 1000;
        int weekday = (int) floorMod(epochDay + 4, 7);

        StringBuilder output = new StringBuilder(pattern.length() + 16);
        for (int index = 0; index < pattern.length();) {
            char token = pattern.charAt(index);
            if (token == '\'') {
                index++;
                while (index < pattern.length() &&
                        pattern.charAt(index) != '\'') {
                    output.append(pattern.charAt(index++));
                }
                if (index < pattern.length()) index++;
                continue;
            }
            if (!isPatternLetter(token)) {
                output.append(token);
                index++;
                continue;
            }
            int end = index + 1;
            while (end < pattern.length() &&
                    pattern.charAt(end) == token) {
                end++;
            }
            int count = end - index;
            switch (token) {
                case 'y':
                    int year = count == 2 ? date[0] % 100 : date[0];
                    appendNumber(output, year, count == 1 ? 1 : count);
                    break;
                case 'M':
                    if (count >= 4) {
                        output.append(LONG_MONTHS[date[1] - 1]);
                    } else if (count == 3) {
                        output.append(MONTHS[date[1] - 1]);
                    } else {
                        appendNumber(output, date[1], count);
                    }
                    break;
                case 'd':
                    appendNumber(output, date[2], count);
                    break;
                case 'H':
                    appendNumber(output, hour, count);
                    break;
                case 'h':
                    appendNumber(output, hour % 12 == 0 ? 12 : hour % 12, count);
                    break;
                case 'm':
                    appendNumber(output, minute, count);
                    break;
                case 's':
                    appendNumber(output, second, count);
                    break;
                case 'S':
                    appendNumber(output, millisecond, Math.max(3, count));
                    break;
                case 'a':
                    output.append(hour < 12 ? "AM" : "PM");
                    break;
                case 'E':
                    output.append(WEEKDAYS[weekday]);
                    break;
                case 'z':
                    output.append(timeZone.getID());
                    break;
                case 'Z':
                    appendZone(output, timeZone.getRawOffset());
                    break;
                default:
                    for (int repeat = 0; repeat < count; repeat++) {
                        output.append(token);
                    }
                    break;
            }
            index = end;
        }
        return output.toString();
    }

    private long parsePattern(String source) throws ParseException {
        int year = 1970;
        int month = 1;
        int day = 1;
        int hour = 0;
        int minute = 0;
        int second = 0;
        int millisecond = 0;
        boolean twelveHour = false;
        boolean afternoon = false;
        int cursor = 0;

        for (int index = 0; index < pattern.length();) {
            char token = pattern.charAt(index);
            if (token == '\'') {
                index++;
                while (index < pattern.length() &&
                        pattern.charAt(index) != '\'') {
                    if (cursor >= source.length() ||
                            source.charAt(cursor++) != pattern.charAt(index++)) {
                        throw new ParseException("Unparseable date", cursor);
                    }
                }
                if (index < pattern.length()) index++;
                continue;
            }
            if (!isPatternLetter(token)) {
                if (cursor >= source.length() ||
                        source.charAt(cursor++) != token) {
                    throw new ParseException("Unparseable date", cursor);
                }
                index++;
                continue;
            }
            int end = index + 1;
            while (end < pattern.length() &&
                    pattern.charAt(end) == token) {
                end++;
            }
            int count = end - index;
            int value = 0;
            if (isDigitToken(token)) {
                int width = token == 'M' && count >= 3 ? 0 :
                    (count == 1 ? 1 : count);
                if (width > 0) {
                    if (cursor + width > source.length()) {
                        throw new ParseException("Unparseable date", cursor);
                    }
                    for (int digit = 0; digit < width; digit++) {
                        char character = source.charAt(cursor++);
                        if (character < '0' || character > '9') {
                            throw new ParseException("Unparseable date", cursor);
                        }
                        value = value * 10 + character - '0';
                    }
                }
            }
            switch (token) {
                case 'y': year = value < 100 ? 2000 + value : value; break;
                case 'M': month = value; break;
                case 'd': day = value; break;
                case 'H': hour = value; break;
                case 'h': hour = value; twelveHour = true; break;
                case 'm': minute = value; break;
                case 's': second = value; break;
                case 'S':
                    millisecond = value;
                    for (int digits = count; digits < 3; digits++) {
                        millisecond *= 10;
                    }
                    break;
                case 'a':
                    if (cursor + 2 <= source.length()) {
                        String marker = source.substring(cursor, cursor + 2);
                        if ("PM".equalsIgnoreCase(marker)) afternoon = true;
                        else if (!"AM".equalsIgnoreCase(marker)) {
                            throw new ParseException("Unparseable date", cursor);
                        }
                        cursor += 2;
                    }
                    break;
                default:
                    cursor += count;
                    break;
            }
            index = end;
        }
        if (cursor != source.length()) {
            throw new ParseException("Unparseable date", cursor);
        }
        if (twelveHour) {
            hour %= 12;
            if (afternoon) hour += 12;
        }
        if (month < 1 || month > 12 || day < 1 ||
                day > lengthOfMonth(year, month) ||
                hour > 23 || minute > 59 || second > 60) {
            throw new ParseException("Unparseable date", 0);
        }
        long epochDay = toEpochDay(year, month, day);
        long localMillis = epochDay * 86_400_000L +
            hour * 3_600_000L + minute * 60_000L +
            second * 1000L + millisecond;
        return localMillis - timeZone.getOffset(
            localMillis - timeZone.getRawOffset());
    }

    private static boolean isPatternLetter(char value) {
        return value >= 'a' && value <= 'z' ||
            value >= 'A' && value <= 'Z';
    }

    private static boolean isDigitToken(char value) {
        return value == 'y' || value == 'M' || value == 'd' ||
            value == 'H' || value == 'h' || value == 'm' ||
            value == 's' || value == 'S';
    }

    private static void appendNumber(StringBuilder output, int value, int width) {
        String text = Integer.toString(value);
        for (int index = text.length(); index < width; index++) {
            output.append('0');
        }
        output.append(text);
    }

    private static void appendZone(StringBuilder output, int offset) {
        int minutes = offset / 60_000;
        output.append(minutes < 0 ? '-' : '+');
        minutes = minutes < 0 ? -minutes : minutes;
        appendNumber(output, minutes / 60, 2);
        appendNumber(output, minutes % 60, 2);
    }

    private static int[] dateFromEpochDay(long epochDay) {
        int low = -999_999_999;
        int high = 999_999_999;
        while (low < high) {
            int middle = low + (high - low + 1) / 2;
            if (toEpochDay(middle, 1, 1) <= epochDay) {
                low = middle;
            } else {
                high = middle - 1;
            }
        }
        int dayOfYear = (int) (epochDay - toEpochDay(low, 1, 1)) + 1;
        int month = 1;
        while (dayOfYear > lengthOfMonth(low, month)) {
            dayOfYear -= lengthOfMonth(low, month++);
        }
        return new int[] {low, month, dayOfYear};
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
    private static final String[] MONTHS =
        {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    private static final String[] LONG_MONTHS =
        {"January", "February", "March", "April", "May", "June",
         "July", "August", "September", "October", "November", "December"};
    private static final String[] WEEKDAYS =
        {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    // ---- overlay：Format 面 API 补全 ----
    @Override
    public Object parseObject(String source, java.text.ParsePosition pos) {
        try {
            return parse(source);
        } catch (java.text.ParseException e) {
            return null;
        }
    }

    public void set2DigitYearStart(Date startDate) {
    }

}
