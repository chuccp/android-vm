package javax.security.cert;

@Deprecated(since="9")
public class CertificateException extends Exception {

    private static final long serialVersionUID = -5757213374030785290L;

    public CertificateException() {
        super();
    }

    public CertificateException(String msg) {
        super(msg);
    }
}
