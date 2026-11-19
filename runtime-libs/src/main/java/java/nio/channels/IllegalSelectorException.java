package java.nio.channels;

/** NIO 通道语义异常（JDK 同形；消息面 illegal selector）。 */
public class IllegalSelectorException extends IllegalArgumentException {

    private static final long serialVersionUID = 1L;

    public IllegalSelectorException() {
        super("illegal selector");
    }

    public IllegalSelectorException(String message) {
        super(message);
    }
}
