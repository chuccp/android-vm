package java.util;

public class TimeZone {
    private static TimeZone defaultZone = new TimeZone("GMT");

    private String id;
    private int rawOffset;

    public TimeZone(String id) {
        this.id = canonicalId(id);
        this.rawOffset = rawOffsetFor(this.id);
    }

    public static TimeZone getDefault() {
        return defaultZone;
    }

    public static void setDefault(TimeZone zone) {
        if (zone == null) throw new NullPointerException("zone");
        defaultZone = zone;
    }

    public static TimeZone getTimeZone(String id) {
        return new TimeZone(id);
    }

    public String getID() {
        return id;
    }

    public void setID(String id) {
        this.id = canonicalId(id);
    }

    public int getRawOffset() {
        return rawOffset;
    }

    public void setRawOffset(int offset) {
        rawOffset = offset;
    }

    public int getDSTSavings() {
        return 0;
    }

    public int getOffset(long date) {
        return rawOffset;
    }

    public boolean useDaylightTime() {
        return false;
    }

    public String getDisplayName() {
        return id;
    }

    @Override
    public Object clone() {
        TimeZone copy = new TimeZone(id);
        copy.rawOffset = rawOffset;
        return copy;
    }

    private static String canonicalId(String value) {
        return value == null || value.isEmpty() ||
            "UTC".equals(value) || "Etc/UTC".equals(value) ||
            "Etc/GMT".equals(value) ? "GMT" : value;
    }

    private static int rawOffsetFor(String id) {
        if ("Asia/Shanghai".equals(id) ||
                "Asia/Chongqing".equals(id) ||
                "Asia/Harbin".equals(id)) {
            return 8 * 60 * 60 * 1000;
        }
        if ("Asia/Tokyo".equals(id) || "Asia/Seoul".equals(id)) {
            return 9 * 60 * 60 * 1000;
        }
        if ("America/New_York".equals(id) ||
                "America/Toronto".equals(id)) {
            return -5 * 60 * 60 * 1000;
        }
        if ("America/Los_Angeles".equals(id)) {
            return -8 * 60 * 60 * 1000;
        }
        return 0;
    }

    // ---- overlay：ctor/SHORT/LONG/NO_TIMEZONE/getOffsets/toZoneId API 补全 ----
    public static final int SHORT = 3;
    public static final int LONG = 4;

    public static final TimeZone NO_TIMEZONE = null;

    protected TimeZone() {
        this.id = "GMT";
    }

    public TimeZone(String id, int rawOffset) {
        this.id = id;
        this.rawOffset = rawOffset;
    }

    int getOffsets(long time, int[] offsets) {
        int raw = getRawOffset();
        int dst = getOffset(time) - raw;
        if (offsets != null) {
            offsets[0] = raw;
            offsets[1] = dst;
        }
        return raw + dst;
    }

    public int getOffsets(int era, int year, int month, int day,
                          int dayOfWeek, int milliseconds, int[] offsets) {
        return getOffsets(0L, offsets);
    }

    public String getDisplayName(boolean daylight, int style) {
        return getDisplayName();
    }

    public String getDisplayName(boolean daylight, int style, Locale locale) {
        return getDisplayName();
    }

    public String getDisplayName(Locale locale) {
        return getDisplayName();
    }

    public java.time.ZoneId toZoneId() {
        return java.time.ZoneId.of(getID());
    }
}
