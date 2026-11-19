package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 unsupported address type）。 */
public class UnsupportedAddressTypeException extends IllegalArgumentException {

    private static final long serialVersionUID = 1L;

    public UnsupportedAddressTypeException() {
        super("unsupported address type");
    }

    public UnsupportedAddressTypeException(String message) {
        super(message);
    }
}
