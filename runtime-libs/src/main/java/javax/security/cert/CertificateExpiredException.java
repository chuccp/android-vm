package javax.security.cert;

@Deprecated(since="9")
public class CertificateExpiredException extends CertificateException {

    private static final long serialVersionUID = 5091601212177261883L;

    public CertificateExpiredException() {
        super();
    }

    public CertificateExpiredException(String message) {
        super(message);
    }
}
