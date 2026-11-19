package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 unresolved address）。 */
public class UnresolvedAddressException extends IllegalArgumentException {

    private static final long serialVersionUID = 1L;

    public UnresolvedAddressException() {
        super("unresolved address");
    }

    public UnresolvedAddressException(String message) {
        super(message);
    }
}
