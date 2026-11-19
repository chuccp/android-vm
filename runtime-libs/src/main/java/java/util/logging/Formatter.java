package java.util.logging;

/** 日志格式化基类。 */
public abstract class Formatter {

    protected Formatter() {
    }

    public abstract String format(LogRecord record);

    public String formatMessage(LogRecord record) {
        return record.getMessage();
    }

    public String getHead(Handler h) {
        return "";
    }

    public String getTail(Handler h) {
        return "";
    }
}
