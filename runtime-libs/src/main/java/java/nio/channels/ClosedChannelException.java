package java.nio.channels;

import java.io.IOException;

/** NIO 通道语义异常（JDK 同形；消息面 closed）。 */
public class ClosedChannelException extends IOException {

    private static final long serialVersionUID = 1L;

    public ClosedChannelException() {
        super("closed");
    }

    public ClosedChannelException(String message) {
        super(message);
    }
}
