package java.util.concurrent;

public class RejectedExecutionException extends RuntimeException {
    public RejectedExecutionException() {
        super();
    }

    public RejectedExecutionException(String message) {
        super(message);
    }

    public RejectedExecutionException(Throwable cause) {
        super(cause);
    }
}
