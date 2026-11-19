package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 not yet connected）。 */
public class NotYetConnectedException extends IllegalStateException {

    private static final long serialVersionUID = 1L;

    public NotYetConnectedException() {
        super("not yet connected");
    }

    public NotYetConnectedException(String message) {
        super(message);
    }
}
