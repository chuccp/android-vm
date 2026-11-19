package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 illegal blocking mode）。 */
public class IllegalBlockingModeException extends IllegalStateException {

    private static final long serialVersionUID = 1L;

    public IllegalBlockingModeException() {
        super("illegal blocking mode");
    }

    public IllegalBlockingModeException(String message) {
        super(message);
    }
}
