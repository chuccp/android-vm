package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 connection pending）。 */
public class ConnectionPendingException extends IllegalStateException {

    private static final long serialVersionUID = 1L;

    public ConnectionPendingException() {
        super("connection pending");
    }

    public ConnectionPendingException(String message) {
        super(message);
    }
}
