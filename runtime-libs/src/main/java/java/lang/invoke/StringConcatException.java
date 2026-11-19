package java.lang.invoke;

public class StringConcatException extends Exception {
    private static final long serialVersionUID = 292L + 9L;

    public StringConcatException(String msg) {
        super(msg);
    }

    public StringConcatException(String msg, Throwable cause) {
        super(msg, cause);
    }
}
