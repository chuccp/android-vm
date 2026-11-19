package java.time.temporal;

import static java.time.temporal.ChronoField.EPOCH_DAY;
import static java.time.temporal.ChronoField.NANO_OF_DAY;
import static java.time.temporal.ChronoField.OFFSET_SECONDS;

import java.time.LocalDate;
import java.time.LocalTime;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.time.chrono.Chronology;

public final class TemporalQueries {

    private TemporalQueries() {
    }

    public static TemporalQuery<ZoneId> zoneId() {
        return TemporalQueries.ZONE_ID;
    }

    public static TemporalQuery<Chronology> chronology() {
        return TemporalQueries.CHRONO;
    }

    public static TemporalQuery<TemporalUnit> precision() {
        return TemporalQueries.PRECISION;
    }

    public static TemporalQuery<ZoneId> zone() {
        return TemporalQueries.ZONE;
    }

    public static TemporalQuery<ZoneOffset> offset() {
        return TemporalQueries.OFFSET;
    }

    public static TemporalQuery<LocalDate> localDate() {
        return TemporalQueries.LOCAL_DATE;
    }

    public static TemporalQuery<LocalTime> localTime() {
        return TemporalQueries.LOCAL_TIME;
    }

    static final TemporalQuery<ZoneId> ZONE_ID = new TemporalQuery<>() {
        @Override
        public ZoneId queryFrom(TemporalAccessor temporal) {
            return temporal.query(TemporalQueries.ZONE_ID);
        }

        @Override
        public String toString() {
            return "ZoneId";
        }
    };

    static final TemporalQuery<Chronology> CHRONO = new TemporalQuery<>() {
        @Override
        public Chronology queryFrom(TemporalAccessor temporal) {
            return temporal.query(TemporalQueries.CHRONO);
        }

        @Override
        public String toString() {
            return "Chronology";
        }
    };

    static final TemporalQuery<TemporalUnit> PRECISION = new TemporalQuery<>() {
        @Override
        public TemporalUnit queryFrom(TemporalAccessor temporal) {
            return temporal.query(TemporalQueries.PRECISION);
        }

        @Override
        public String toString() {
            return "Precision";
        }
    };

    static final TemporalQuery<ZoneOffset> OFFSET = new TemporalQuery<>() {
        @Override
        public ZoneOffset queryFrom(TemporalAccessor temporal) {
            if (temporal.isSupported(OFFSET_SECONDS)) {
                return ZoneOffset.ofTotalSeconds(temporal.get(OFFSET_SECONDS));
            }
            return null;
        }

        @Override
        public String toString() {
            return "ZoneOffset";
        }
    };

    static final TemporalQuery<ZoneId> ZONE = new TemporalQuery<>() {
        @Override
        public ZoneId queryFrom(TemporalAccessor temporal) {
            ZoneId zone = temporal.query(ZONE_ID);
            return (zone != null ? zone : temporal.query(OFFSET));
        }

        @Override
        public String toString() {
            return "Zone";
        }
    };

    static final TemporalQuery<LocalDate> LOCAL_DATE = new TemporalQuery<>() {
        @Override
        public LocalDate queryFrom(TemporalAccessor temporal) {
            if (temporal.isSupported(EPOCH_DAY)) {
                return LocalDate.ofEpochDay(temporal.getLong(EPOCH_DAY));
            }
            return null;
        }

        @Override
        public String toString() {
            return "LocalDate";
        }
    };

    static final TemporalQuery<LocalTime> LOCAL_TIME = new TemporalQuery<>() {
        @Override
        public LocalTime queryFrom(TemporalAccessor temporal) {
            if (temporal.isSupported(NANO_OF_DAY)) {
                return LocalTime.ofNanoOfDay(temporal.getLong(NANO_OF_DAY));
            }
            return null;
        }

        @Override
        public String toString() {
            return "LocalTime";
        }
    };

}
