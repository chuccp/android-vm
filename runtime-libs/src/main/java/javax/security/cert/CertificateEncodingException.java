package javax.security.cert;

@Deprecated(since="9")
public class CertificateEncodingException extends CertificateException {

    private static final long serialVersionUID = -8187642723048403470L;

    public CertificateEncodingException() {
        super();
    }

    public CertificateEncodingException(String message) {
        super(message);
    }
}
