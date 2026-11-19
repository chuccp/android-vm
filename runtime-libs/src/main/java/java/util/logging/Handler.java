package java.util.logging;

/** 日志处理器基类（级别过滤 + 格式化 + 发布）。 */
public abstract class Handler {

    private Level level = Level.ALL;
    private Formatter formatter;

    protected Handler() {
    }

    public abstract void publish(LogRecord record);

    public abstract void flush();

    public abstract void close();

    public Level getLevel() {
        return level;
    }

    public void setLevel(Level newLevel) {
        if (newLevel == null) {
            throw new NullPointerException("level");
        }
        this.level = newLevel;
    }

    public Formatter getFormatter() {
        return formatter;
    }

    public void setFormatter(Formatter newFormatter) {
        this.formatter = newFormatter;
    }

    public java.nio.charset.Charset getEncoding() {
        return null;
    }

    public boolean isLoggable(LogRecord record) {
        return record != null && record.getLevel().intValue() >= level.intValue();
    }
}
