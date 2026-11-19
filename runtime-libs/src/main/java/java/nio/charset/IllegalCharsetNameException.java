package java.nio.charset;

/**
 * 非法字符集名（OpenJDK 11 语义；Charset.checkName 抛出）。
 */
public class IllegalCharsetNameException extends IllegalArgumentException {

    private static final long serialVersionUID = 1457525358470002989L;

    private final String charsetName;

    public IllegalCharsetNameException(String charsetName) {
        super(String.valueOf(charsetName));
        this.charsetName = charsetName;
    }

    public String getCharsetName() {
        return charsetName;
    }
}
