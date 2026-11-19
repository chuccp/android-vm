package java.net.spi;

import java.net.URLStreamHandlerFactory;

public abstract class URLStreamHandlerProvider
    implements URLStreamHandlerFactory
{
    private static Void checkPermission() {
        SecurityManager sm = System.getSecurityManager();
        if (sm != null)
            sm.checkPermission(new RuntimePermission("setFactory"));
        return null;
    }
    private URLStreamHandlerProvider(Void ignore) { }

    protected URLStreamHandlerProvider() {
        this(checkPermission());
    }
}
