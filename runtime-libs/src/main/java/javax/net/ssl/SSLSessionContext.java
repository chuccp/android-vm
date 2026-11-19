package javax.net.ssl;

import java.util.Enumeration;

public interface SSLSessionContext {

    public SSLSession getSession(byte[] sessionId);

    public Enumeration<byte[]> getIds();

    public void setSessionTimeout(int seconds)
                 throws IllegalArgumentException;

    public int getSessionTimeout();

    public void setSessionCacheSize(int size)
                 throws IllegalArgumentException;

    public int getSessionCacheSize();
}
