package javax.security.cert;

@Deprecated(since="9")
public class CertificateNotYetValidException extends CertificateException {

    private static final long serialVersionUID = -8976172474266822818L;

    public CertificateNotYetValidException() {
        super();
    }

    public CertificateNotYetValidException(String message) {
        super(message);
    }
}
