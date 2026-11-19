package javax.net.ssl;

import java.security.PrivateKey;
import java.security.Principal;
import java.security.cert.X509Certificate;
import java.net.Socket;

public interface X509KeyManager extends KeyManager {

    public String[] getClientAliases(String keyType, Principal[] issuers);

    public String chooseClientAlias(String[] keyType, Principal[] issuers,
        Socket socket);

    public String[] getServerAliases(String keyType, Principal[] issuers);

    public String chooseServerAlias(String keyType, Principal[] issuers,
        Socket socket);

    public X509Certificate[] getCertificateChain(String alias);

    public PrivateKey getPrivateKey(String alias);
}
