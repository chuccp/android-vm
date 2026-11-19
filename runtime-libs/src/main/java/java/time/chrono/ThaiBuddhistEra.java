package java.time.chrono;

import static java.time.temporal.ChronoField.ERA;

import java.time.DateTimeException;
import java.time.format.DateTimeFormatterBuilder;
import java.time.format.TextStyle;
import java.util.Locale;

public enum ThaiBuddhistEra implements Era {

    BEFORE_BE,

    BE;

    public static ThaiBuddhistEra of(int thaiBuddhistEra) {
        switch (thaiBuddhistEra) {
            case 0:
                return BEFORE_BE;
            case 1:
                return BE;
            default:
                throw new DateTimeException("Invalid era: " + thaiBuddhistEra);
        }
    }

    @Override
    public int getValue() {
        return ordinal();
    }

    @Override
    public String getDisplayName(TextStyle style, Locale locale) {
        return new DateTimeFormatterBuilder()
            .appendText(ERA, style)
            .toFormatter(locale)
            .withChronology(ThaiBuddhistChronology.INSTANCE)
            .format(this == BE ? ThaiBuddhistDate.of(1, 1, 1) : ThaiBuddhistDate.of(0, 1, 1));
    }

}
