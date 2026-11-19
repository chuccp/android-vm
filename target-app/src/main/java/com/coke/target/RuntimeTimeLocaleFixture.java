package com.coke.target;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

public final class RuntimeTimeLocaleFixture {
    public int timeLocaleFlow() throws Exception {
        Locale locale = Locale.forLanguageTag("zh-CN");
        if (!"zh".equals(locale.getLanguage()) ||
                !"CN".equals(locale.getCountry()) ||
                !"zh-CN".equals(locale.toLanguageTag())) {
            return -1;
        }
        Locale typed = new Locale("en", "US");
        Locale same = new Locale("en", "US");
        if (!typed.equals(same) ||
                typed.hashCode() != same.hashCode() ||
                Locale.getDefault().getLanguage().isEmpty()) {
            return -2;
        }

        Date first = new Date(1000L);
        Date second = new Date(2000L);
        if (first.getTime() != 1000L ||
                !first.before(second) ||
                !second.after(first) ||
                first.compareTo(second) >= 0 ||
                first.equals(second)) {
            return -3;
        }
        Date cloned = (Date) first.clone();
        first.setTime(1500L);
        if (cloned.getTime() != 1000L ||
                first.getTime() != 1500L ||
                first.hashCode() == second.hashCode()) {
            return -4;
        }

        SimpleDateFormat format =
                new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
        format.setTimeZone(TimeZone.getTimeZone("UTC"));
        if (!"1970-01-01 00:00:00".equals(
                format.format(new Date(0L)))) {
            return -5;
        }
        Object genericDate = new Date(0L);
        if (!"1970-01-01 00:00:00".equals(
                format.format(genericDate))) {
            return -5;
        }
        Object timestamp = Long.valueOf(0L);
        if (!"1970-01-01 00:00:00".equals(
                format.format(timestamp))) {
            return -5;
        }
        Date parsed = format.parse("1970-01-02 03:04:05");
        if (parsed.getTime() != 97445000L) {
            return -6;
        }
        return 99;
    }
}
