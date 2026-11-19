package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 not yet bound）。 */
public class NotYetBoundException extends IllegalStateException {

    private static final long serialVersionUID = 1L;

    public NotYetBoundException() {
        super("not yet bound");
    }

    public NotYetBoundException(String message) {
        super(message);
    }
}
