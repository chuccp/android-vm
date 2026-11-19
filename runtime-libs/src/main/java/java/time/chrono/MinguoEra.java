package java.time.chrono;

import static java.time.temporal.ChronoField.ERA;

import java.time.DateTimeException;
import java.time.format.DateTimeFormatterBuilder;
import java.time.format.TextStyle;
import java.util.Locale;

public enum MinguoEra implements Era {

    BEFORE_ROC,

    ROC;

    public static MinguoEra of(int minguoEra) {
        switch (minguoEra) {
            case 0:
                return BEFORE_ROC;
            case 1:
                return ROC;
            default:
                throw new DateTimeException("Invalid era: " + minguoEra);
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
            .withChronology(MinguoChronology.INSTANCE)
            .format(this == ROC ? MinguoDate.of(1, 1, 1) : MinguoDate.of(0, 1, 1));
    }

}
