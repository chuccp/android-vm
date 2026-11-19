package javax.net.ssl;

public interface HostnameVerifier {

    public boolean verify(String hostname, SSLSession session);
}
