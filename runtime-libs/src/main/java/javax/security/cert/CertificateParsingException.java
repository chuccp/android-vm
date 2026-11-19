package javax.security.cert;

@Deprecated(since="9")
public class CertificateParsingException extends CertificateException {

    private static final long serialVersionUID = -8449352422951136229L;

    public CertificateParsingException() {
        super();
    }

    public CertificateParsingException(String message) {
        super(message);
    }
}
