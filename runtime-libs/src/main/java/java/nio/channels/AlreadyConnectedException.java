package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 already connected）。 */
public class AlreadyConnectedException extends IllegalStateException {

    private static final long serialVersionUID = 1L;

    public AlreadyConnectedException() {
        super("already connected");
    }

    public AlreadyConnectedException(String message) {
        super(message);
    }
}
