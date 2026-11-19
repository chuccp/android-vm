package android.text.format;

import android.content.Context;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * 日期格式化工具（AOSP android.text.format.DateUtils 常用静态面；
 * VM 以 java.text.SimpleDateFormat 本地实现，时区/语言取默认）。
 */
public class DateUtils {
    public static final int FORMAT_SHOW_TIME = 0x00001;
    public static final int FORMAT_SHOW_WEEKDAY = 0x00002;
    public static final int FORMAT_SHOW_YEAR = 0x00004;
    public static final int FORMAT_NO_YEAR = 0x00008;
    public static final int FORMAT_SHOW_DATE = 0x00010;
    public static final int FORMAT_NO_MONTH_DAY = 0x00020;
    public static final int FORMAT_NO_NOON = 0x00040;
    public static final int FORMAT_NO_MIDNIGHT = 0x00080;
    public static final int FORMAT_ABBREV_MONTH = 0x00100;
    public static final int FORMAT_ABBREV_ALL = 0x00200;
    public static final int FORMAT_CAPITALIZE = 0x00400;

    public static CharSequence getRelativeTimeSpanString(long time) {
        return getRelativeTimeSpanString(time, System.currentTimeMillis(),
                MINUTE_IN_MILLIS);
    }

    public static CharSequence getRelativeTimeSpanString(long time, long now,
                                                         long minResolution) {
        long delta = now - time;
        if (delta < 0) delta = -delta;
        if (delta < MINUTE_IN_MILLIS) return "刚刚";
        if (delta < HOUR_IN_MILLIS) return (delta / MINUTE_IN_MILLIS) + "分钟前";
        if (delta < DAY_IN_MILLIS) return (delta / HOUR_IN_MILLIS) + "小时前";
        if (delta < WEEK_IN_MILLIS) return (delta / DAY_IN_MILLIS) + "天前";
        return formatDateTimePlain(time, "yyyy-MM-dd");
    }

    public static CharSequence getRelativeTimeSpanString(long time, long now, long minResolution,
                                                         int flags) {
        return getRelativeTimeSpanString(time, now, minResolution);
    }

    public static CharSequence getRelativeDateTimeString(Context c, long time, long transitionResolution,
                                                         long minResolution, int flags) {
        return getRelativeTimeSpanString(time);
    }

    public static String formatDateTime(Context context, long millis, int flags) {
        return formatDateTimePlain(millis, "yyyy-MM-dd HH:mm");
    }

    public static String formatDateRange(Context context, long startMillis, long endMillis, int flags) {
        if (startMillis == endMillis) return formatDateTimePlain(startMillis, "yyyy-MM-dd");
        return formatDateTimePlain(startMillis, "yyyy-MM-dd") + " - " +
                formatDateTimePlain(endMillis, "yyyy-MM-dd");
    }

    public static java.util.Formatter formatDateRange(Context context, java.util.Formatter formatter,
                                                      long startMillis, long endMillis, int flags) {
        formatter.format("%s", formatDateRange(context, startMillis, endMillis, flags));
        return formatter;
    }

    public static String formatElapsedTime(long elapsedSeconds) {
        long hours = elapsedSeconds / 3600;
        long minutes = (elapsedSeconds % 3600) / 60;
        long seconds = elapsedSeconds % 60;
        if (hours > 0) {
            return String.format(Locale.US, "%d:%02d:%02d", hours, minutes, seconds);
        }
        return String.format(Locale.US, "%d:%02d", minutes, seconds);
    }

    public static boolean isToday(long when) {
        SimpleDateFormat day = new SimpleDateFormat("yyyyMMdd", Locale.getDefault());
        return day.format(new Date(when)).equals(day.format(new Date()));
    }

    public static final long SECOND_IN_MILLIS = 1000;
    public static final long MINUTE_IN_MILLIS = 60 * SECOND_IN_MILLIS;
    public static final long HOUR_IN_MILLIS = 60 * MINUTE_IN_MILLIS;
    public static final long DAY_IN_MILLIS = 24 * HOUR_IN_MILLIS;
    public static final long WEEK_IN_MILLIS = 7 * DAY_IN_MILLIS;

    private static String formatDateTimePlain(long millis, String pattern) {
        return new SimpleDateFormat(pattern, Locale.getDefault()).format(new Date(millis));
    }
}
