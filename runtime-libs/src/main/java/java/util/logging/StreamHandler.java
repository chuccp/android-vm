package java.util.logging;

import java.io.PrintStream;

/** 输出流处理器（Formatter → 流；级别过滤在 Handler.isLoggable）。 */
public class StreamHandler extends Handler {

    private PrintStream output;
    private boolean closed;

    public StreamHandler() {
    }

    public StreamHandler(PrintStream output, Formatter formatter) {
        this.output = output;
        setFormatter(formatter);
    }

    protected void setOutputStream(PrintStream output) {
        this.output = output;
        this.closed = false;
    }

    public PrintStream getOutputStream() {
        return output;
    }

    @Override
    public void publish(LogRecord record) {
        if (closed || !isLoggable(record) || output == null) {
            return;
        }
        Formatter f = getFormatter();
        output.print(f != null ? f.format(record) : record.getMessage() + "\n");
    }

    @Override
    public void flush() {
        if (output != null) {
            output.flush();
        }
    }

    @Override
    public void close() {
        flush();
        closed = true;
        output = null;
    }
}
