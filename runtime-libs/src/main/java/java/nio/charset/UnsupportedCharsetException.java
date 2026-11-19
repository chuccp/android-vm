package java.nio.charset;

/**
 * 不支持的字符集名（OpenJDK 11 语义；Charset.forName 未命中时抛出）。
 * 此前 lib 缺类导致 OkHttp Util.<clinit> 的 Charset.forName 失败。
 */
public class UnsupportedCharsetException extends IllegalArgumentException {

    private static final long serialVersionUID = 1490765524727386367L;

    private final String charsetName;

    public UnsupportedCharsetException(String charsetName) {
        super(String.valueOf(charsetName));
        this.charsetName = charsetName;
    }

    public String getCharsetName() {
        return charsetName;
    }
}
