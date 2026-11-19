package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 already bound）。 */
public class AlreadyBoundException extends IllegalStateException {

    private static final long serialVersionUID = 1L;

    public AlreadyBoundException() {
        super("already bound");
    }

    public AlreadyBoundException(String message) {
        super(message);
    }
}
