package java.util.logging;

import java.io.Serializable;

/** 日志记录（netty JdkLogger / JUL 消费面）。 */
public class LogRecord implements Serializable {

    private static final long serialVersionUID = 5372048053134512534L;

    private Level level;
    private String message;
    private Object[] parameters;
    private String loggerName;
    private String sourceClassName;
    private String sourceMethodName;
    private Throwable thrown;
    private long millis = System.currentTimeMillis();
    private int threadID;
    private long sequenceNumber;

    public LogRecord(Level level, String msg) {
        this.level = level;
        this.message = msg;
        this.threadID = (int) Thread.currentThread().getId();
    }

    public Level getLevel() {
        return level;
    }

    public void setLevel(Level level) {
        this.level = level;
    }

    public String getMessage() {
        return message;
    }

    public void setMessage(String message) {
        this.message = message;
    }

    public Object[] getParameters() {
        return parameters;
    }

    public void setParameters(Object[] parameters) {
        this.parameters = parameters;
    }

    public String getLoggerName() {
        return loggerName;
    }

    public void setLoggerName(String loggerName) {
        this.loggerName = loggerName;
    }

    public String getSourceClassName() {
        return sourceClassName;
    }

    public void setSourceClassName(String sourceClassName) {
        this.sourceClassName = sourceClassName;
    }

    public String getSourceMethodName() {
        return sourceMethodName;
    }

    public void setSourceMethodName(String sourceMethodName) {
        this.sourceMethodName = sourceMethodName;
    }

    public Throwable getThrown() {
        return thrown;
    }

    public void setThrown(Throwable thrown) {
        this.thrown = thrown;
    }

    public long getMillis() {
        return millis;
    }

    public void setMillis(long millis) {
        this.millis = millis;
    }

    public int getThreadID() {
        return threadID;
    }

    public void setThreadID(int threadID) {
        this.threadID = threadID;
    }

    public long getSequenceNumber() {
        return sequenceNumber;
    }

    public void setSequenceNumber(long sequenceNumber) {
        this.sequenceNumber = sequenceNumber;
    }
}
