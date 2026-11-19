package java.lang.reflect;

import java.io.Serializable;

/**
 * 代理实例生成（§31.7 C 档 lib 本体 + VM 薄缝）。
 *
 * <p>补源口径（vendored OpenJDK 形态依赖 jdk.internal.* / ProxyBuilder JVM
 * defineProxyClass 机器，jre-excluded 不可编译，见 build/runtime-packaging/
 * uncompilable.txt）：Java 本体保留在 lib——API 形状（getProxyClass/
 * newProxyInstance/isProxyClass/getInvocationHandler）与缓存/参数校验全在 Java；
 * VM 侧只留两个 native 接缝：
 *
 * <ul>
 *   <li>{@code defineProxyClass0}：合成代理类（super=本类、实现给定接口族、
 *       每方法一条分发 intrinsic 落到 InvocationHandler.invoke）；
 *   <li>{@code isProxyClass0}：合成类登记判定。
 * </ul>
 *
 * <p>分发语义（OpenJDK 生成字节码的等价物）：接口全部实例方法（含 default 与
 * 超接口）以及 Object 的 equals/hashCode/toString 路由到 handler.invoke(this,
 * method, args)；返回值按方法声明返回型拆箱（handler 返回 null 拆箱 = NPE）。
 * handler 抛出按原异常穿出（UndeclaredThrowableException 包装后置）。
 */
public class Proxy implements Serializable {
    private static final long serialVersionUID = -2222568056686623797L;

    /** handler 分发槽（子类实例经分发 intrinsic 写入）。 */
    protected InvocationHandler h;

    protected Proxy(InvocationHandler h) {
        if (h == null) {
            throw new NullPointerException("handler == null");
        }
        this.h = h;
    }

    private Proxy() {
        this.h = null;
    }

    /**
     * 代理类（幂等：同 loader + 接口族同一 Class；VM 侧合成登记）。
     */
    public static Class<?> getProxyClass(ClassLoader loader, Class<?>... interfaces) {
        return defineProxyClass0(loader, interfaces);
    }

    /**
     * 代理实例（handler 路由全部接口方法与 Object 公共面）。
     */
    public static Object newProxyInstance(ClassLoader loader, Class<?>[] interfaces,
                                          InvocationHandler h) {
        if (h == null) {
            throw new NullPointerException("handler == null");
        }
        Class<?> proxyClass = defineProxyClass0(loader, interfaces);
        try {
            Constructor<?> ctor = proxyClass.getDeclaredConstructor(InvocationHandler.class);
            return ctor.newInstance(new Object[] { h });
        } catch (ReflectiveOperationException e) {
            throw new InternalError("cannot instantiate proxy " + proxyClass, e);
        }
    }

    public static boolean isProxyClass(Class<?> cl) {
        return cl != null && isProxyClass0(cl);
    }

    public static InvocationHandler getInvocationHandler(Object proxy) {
        if (proxy == null) {
            throw new NullPointerException("proxy == null");
        }
        if (!isProxyClass(proxy.getClass())) {
            throw new IllegalArgumentException("not a proxy instance: " + proxy.getClass());
        }
        return ((Proxy) proxy).h;
    }

    // ---- VM 分发接缝（§31.7 薄缝；core/builtins.cc 绑定）----

    private static native Class<?> defineProxyClass0(ClassLoader loader, Class<?>[] interfaces);

    private static native boolean isProxyClass0(Class<?> cl);
}
