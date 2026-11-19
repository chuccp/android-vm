package javax.net.ssl;

import java.util.EventListener;

public
interface SSLSessionBindingListener
extends EventListener
{

    public void valueBound(SSLSessionBindingEvent event);

    public void valueUnbound(SSLSessionBindingEvent event);
}
