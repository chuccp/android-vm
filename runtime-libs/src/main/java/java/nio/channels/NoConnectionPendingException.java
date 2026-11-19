package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 no connection pending）。 */
public class NoConnectionPendingException extends IllegalStateException {

    private static final long serialVersionUID = 1L;

    public NoConnectionPendingException() {
        super("no connection pending");
    }

    public NoConnectionPendingException(String message) {
        super(message);
    }
}
