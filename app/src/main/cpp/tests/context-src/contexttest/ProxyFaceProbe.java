package contexttest;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

/**
 * java.lang.reflect.Proxy 面用例（§31.7：lib 本体 + VM 分发缝）。
 * 覆盖：接口方法分发（含装箱/拆箱/void/null 返回）、Object 公共面路由、
 * 元面（isProxyClass/getProxyClass 幂等/getInvocationHandler）、handler 抛出
 * 穿出、Method.invoke(target) 虚派发链（handler 内反射委托真实现）。
 * 每行输出 "name: OK" 或 "name: FAIL ...";harness 按行判定。
 */
public class ProxyFaceProbe {

    public interface Greeter {
        String greet(String who);
        int sum(int a, int b);
        void flip(boolean b);
        Object nothing();
    }

    private static StringBuilder out = new StringBuilder();

    private static void check(String name, boolean ok, String detail) {
        out.append(name).append(": ").append(ok ? "OK" : "FAIL ").append(ok ? "" : detail).append('\n');
    }

    public static String run() {
        final StringBuilder calls = new StringBuilder();
        Greeter g = (Greeter) Proxy.newProxyInstance(
                ProxyFaceProbe.class.getClassLoader(),
                new Class<?>[] { Greeter.class },
                new InvocationHandler() {
                    @Override
                    public Object invoke(Object proxy, Method method, Object[] args) {
                        String name = method.getName();
                        calls.append(name).append(';');
                        if ("greet".equals(name)) return "hi:" + args[0];
                        if ("sum".equals(name)) {
                            return Integer.valueOf(((Integer) args[0]).intValue()
                                    + ((Integer) args[1]).intValue());
                        }
                        if ("flip".equals(name)) return null;
                        if ("nothing".equals(name)) return null;
                        if ("toString".equals(name)) return "proxy-tostring";
                        if ("hashCode".equals(name)) return Integer.valueOf(42);
                        if ("equals".equals(name)) return Boolean.valueOf(proxy == args[0]);
                        return null;
                    }
                });

        boolean ok = g.greet("zhihu").equals("hi:zhihu") && g.sum(3, 4) == 7;
        check("proxyDispatch", ok, calls.toString());

        g.flip(true);
        check("proxyVoid", calls.indexOf("flip") >= 0, calls.toString());

        check("proxyNullReturn", g.nothing() == null, "non-null");

        check("proxyBoxing", g.sum(3, 4) == 7 && g.greet("zhihu").equals("hi:zhihu"),
                "unbox/ref mismatch");

        Object o = (Object) g;
        check("proxyObjectMethods",
                "proxy-tostring".equals(o.toString()) && o.hashCode() == 42 && g.equals(g),
                String.valueOf(o.toString()) + "/" + o.hashCode());

        check("proxyMeta",
                Proxy.isProxyClass(g.getClass()) && !Proxy.isProxyClass(Greeter.class)
                        && Proxy.getProxyClass(ProxyFaceProbe.class.getClassLoader(), Greeter.class)
                                == g.getClass()
                        && Proxy.getInvocationHandler(g) != null,
                "meta face mismatch");

        // handler 抛出按原异常穿出
        boolean threw = false;
        try {
            Greeter bad = (Greeter) Proxy.newProxyInstance(
                    ProxyFaceProbe.class.getClassLoader(),
                    new Class<?>[] { Greeter.class },
                    new InvocationHandler() {
                        @Override
                        public Object invoke(Object proxy, Method method, Object[] args) {
                            throw new IllegalStateException("boom");
                        }
                    });
            bad.greet("x");
        } catch (IllegalStateException e) {
            threw = "boom".equals(e.getMessage());
        }
        check("proxyThrow", threw, "no IllegalStateException(boom)");

        // handler 内 Method.invoke(target) 虚派发（反射委托真实现）
        final Greeter real = new Greeter() {
            @Override
            public String greet(String who) {
                return "real:" + who;
            }

            @Override
            public int sum(int a, int b) {
                return a + b;
            }

            @Override
            public void flip(boolean b) {
            }

            @Override
            public Object nothing() {
                return "obj";
            }
        };
        Greeter g2 = (Greeter) Proxy.newProxyInstance(
                ProxyFaceProbe.class.getClassLoader(),
                new Class<?>[] { Greeter.class },
                new InvocationHandler() {
                    @Override
                    public Object invoke(Object proxy, Method method, Object[] args)
                            throws Throwable {
                        try {
                            return method.invoke(real, args);
                        } catch (InvocationTargetException e) {
                            throw e.getCause();
                        }
                    }
                });
        boolean delegateOk = g2.greet("x").equals("real:x") && g2.sum(1, 2) == 3
                && "obj".equals(g2.nothing());
        check("proxyReflectInvoke", delegateOk, "delegate mismatch");

        return out.toString();
    }
}
