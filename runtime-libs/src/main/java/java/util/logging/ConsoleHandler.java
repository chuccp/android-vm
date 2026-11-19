package java.util.logging;

/** 控制台处理器（System.err；发布后 flush）。 */
public class ConsoleHandler extends StreamHandler {

    public ConsoleHandler() {
        setOutputStream(System.err);
    }

    @Override
    public void publish(LogRecord record) {
        super.publish(record);
        flush();
    }

    @Override
    public void close() {
        flush();
    }
}
