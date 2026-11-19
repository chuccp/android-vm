package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 cancelled key）。 */
public class CancelledKeyException extends IllegalStateException {

    private static final long serialVersionUID = 1L;

    public CancelledKeyException() {
        super("cancelled key");
    }

    public CancelledKeyException(String message) {
        super(message);
    }
}
