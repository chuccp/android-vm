package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 closed selector）。 */
public class ClosedSelectorException extends IllegalStateException {

    private static final long serialVersionUID = 1L;

    public ClosedSelectorException() {
        super("closed selector");
    }

    public ClosedSelectorException(String message) {
        super(message);
    }
}
