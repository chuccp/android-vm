package javax.crypto;

import java.io.*;
import java.net.*;
import java.security.*;
import java.util.jar.*;

final class ProviderVerifier {

    private URL jarURL;
    private Provider provider;
    private boolean savePerms;
    private CryptoPermissions appPerms = null;

    ProviderVerifier(URL jarURL, boolean savePerms) {
        this(jarURL, null, savePerms);
    }

    ProviderVerifier(URL jarURL, Provider provider, boolean savePerms) {
        this.jarURL = jarURL;
        this.provider = provider;
        this.savePerms = savePerms;
    }

    void verify() throws IOException {

        if (!savePerms) {
            return;
        }

        final URL url = jarURL.getProtocol().equalsIgnoreCase("jar")?
                        jarURL : new URL("jar:" + jarURL.toString() + "!/");

        JarFile jf = null;
        try {

            try {
                jf = AccessController.doPrivileged(
                         new PrivilegedExceptionAction<JarFile>() {
                             public JarFile run() throws Exception {
                                 JarURLConnection conn =
                                     (JarURLConnection) url.openConnection();

                                 conn.setUseCaches(false);
                                 return conn.getJarFile();
                             }
                         });
            } catch (java.security.PrivilegedActionException pae) {
                throw new SecurityException("Cannot load " + url.toString(),
                    pae.getCause());
            }

            if (jf != null) {
                JarEntry je = jf.getJarEntry("cryptoPerms");
                if (je == null) {
                    throw new JarException(
                        "Can not find cryptoPerms");
                }
                try {
                    appPerms = new CryptoPermissions();
                    appPerms.load(jf.getInputStream(je));
                } catch (Exception ex) {
                    JarException jex =
                        new JarException("Cannot load/parse" +
                            jarURL.toString());
                    jex.initCause(ex);
                    throw jex;
                }
            }
        } finally {

            if (jf != null) {
                jf.close();
            }
        }
    }

    static void verifyPolicySigned(java.security.cert.Certificate[] certs)
            throws Exception {
    }

    static boolean isTrustedCryptoProvider(Provider provider) {
        return false;
    }

    CryptoPermissions getPermissions() {
        return appPerms;
    }
}
