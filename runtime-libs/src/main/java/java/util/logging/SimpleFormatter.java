package java.util.logging;

/** 单行文本格式化（级别: 消息 [异常]）。 */
public class SimpleFormatter extends Formatter {

    public SimpleFormatter() {
    }

    @Override
    public String format(LogRecord record) {
        StringBuilder sb = new StringBuilder();
        sb.append(record.getLevel().getName());
        sb.append(": ");
        sb.append(formatMessage(record));
        sb.append('\n');
        if (record.getThrown() != null) {
            sb.append(record.getThrown().toString());
            sb.append('\n');
        }
        return sb.toString();
    }
}
