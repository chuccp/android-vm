package java.util.logging;

import java.io.Serializable;

/** 日志级别（java.util.logging 标准常量与整数值）。 */
public class Level implements Serializable {

    private static final long serialVersionUID = -8176160795706313070L;

    public static final Level OFF = new Level("OFF", Integer.MAX_VALUE);
    public static final Level SEVERE = new Level("SEVERE", 1000);
    public static final Level WARNING = new Level("WARNING", 900);
    public static final Level INFO = new Level("INFO", 800);
    public static final Level CONFIG = new Level("CONFIG", 700);
    public static final Level FINE = new Level("FINE", 500);
    public static final Level FINER = new Level("FINER", 400);
    public static final Level FINEST = new Level("FINEST", 300);
    public static final Level ALL = new Level("ALL", Integer.MIN_VALUE);

    private final String name;
    private final int value;

    protected Level(String name, int value) {
        this.name = name;
        this.value = value;
    }

    public String getName() {
        return name;
    }

    public String getResourceBundleName() {
        return null;
    }

    public int intValue() {
        return value;
    }

    @Override
    public String toString() {
        return name;
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Level && ((Level) o).value == value;
    }

    @Override
    public int hashCode() {
        return value;
    }

    public static Level parse(String name) throws IllegalArgumentException {
        if (name == null) {
            throw new NullPointerException("name");
        }
        String trimmed = name.trim();
        Level[] known = { OFF, SEVERE, WARNING, INFO, CONFIG, FINE, FINER, FINEST, ALL };
        for (Level level : known) {
            if (level.name.equalsIgnoreCase(trimmed)) {
                return level;
            }
        }
        try {
            return new Level(trimmed, Integer.parseInt(trimmed));
        } catch (NumberFormatException x) {
            throw new IllegalArgumentException("Bad level \"" + name + "\"");
        }
    }
}
