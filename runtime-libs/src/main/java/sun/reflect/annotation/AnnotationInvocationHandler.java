package sun.reflect.annotation;

import java.lang.annotation.Annotation;
import java.lang.annotation.IncompleteAnnotationException;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Map;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：注解实例分发处理器
 * （OpenJDK sun.reflect.annotation.AnnotationInvocationHandler 同语义）。
 * VM 侧（core/builtins_reflect.cc）解码 dex 注解元素填充 memberValues 后，
 * 经 java.lang.reflect.Proxy 分发接缝生成注解实例——本类承接全部方法路由
 * （成员访问器 / annotationType / equals / hashCode / toString）。
 */
public final class AnnotationInvocationHandler implements InvocationHandler {

    private final Class<? extends Annotation> type;
    private final Map<String, Object> memberValues;

    public AnnotationInvocationHandler(Class<? extends Annotation> type,
                                       Map<String, Object> memberValues) {
        if (type == null || memberValues == null) {
            throw new NullPointerException();
        }
        this.type = type;
        this.memberValues = memberValues;
    }

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) {
        String member = method.getName();
        Class<?>[] params = method.getParameterTypes();

        // equals(Object) / hashCode() / toString()（Object 公共面经分发接缝路由）
        if (params.length == 1 && "equals".equals(member) && params[0] == Object.class) {
            return equalsImpl(args[0]);
        }
        if (params.length == 0) {
            if ("hashCode".equals(member)) {
                return hashCodeImpl();
            }
            if ("toString".equals(member)) {
                return toStringImpl();
            }
            if ("annotationType".equals(member)) {
                return type;
            }
            // 注解成员访问器（无参）：memberValues 缺省且无默认值 = 不完整注解
            Object value = memberValues.get(member);
            if (value == null) {
                Object def = defaultValueOf(member);
                if (def != null) {
                    return def;
                }
                throw new IncompleteAnnotationException(type, member);
            }
            return value;
        }
        throw new InternalError("bad annotation method: " + method);
    }

    /** 成员默认值（注解类型声明面；本 VM 无 getDefaultValue 缝——未声明即缺省）。 */
    private Object defaultValueOf(String member) {
        return null;
    }

    private boolean equalsImpl(Object other) {
        if (other == this) {
            return true;
        }
        if (!type.isInstance(other)) {
            return false;
        }
        try {
            InvocationHandler handler = Proxy.getInvocationHandler(other);
            if (!(handler instanceof AnnotationInvocationHandler)) {
                return false;
            }
            AnnotationInvocationHandler that = (AnnotationInvocationHandler) handler;
            if (!that.type.equals(type)) {
                return false;
            }
            for (Map.Entry<String, Object> e : memberValues.entrySet()) {
                Object mine = e.getValue();
                Object theirs = that.memberValues.get(e.getKey());
                if (!valueEquals(mine, theirs)) {
                    return false;
                }
            }
            return true;
        } catch (IllegalArgumentException notAProxy) {
            return false;
        }
    }

    private static boolean valueEquals(Object a, Object b) {
        if (a == b) {
            return true;
        }
        if (a == null || b == null) {
            return false;
        }
        if (a instanceof Object[] && b instanceof Object[]) {
            Object[] aa = (Object[]) a;
            Object[] bb = (Object[]) b;
            if (aa.length != bb.length) {
                return false;
            }
            for (int i = 0; i < aa.length; i++) {
                if (!valueEquals(aa[i], bb[i])) {
                    return false;
                }
            }
            return true;
        }
        return a.equals(b);
    }

    private int hashCodeImpl() {
        int hash = 0;
        for (Map.Entry<String, Object> e : memberValues.entrySet()) {
            hash += (127 * e.getKey().hashCode()) ^ valueHashCode(e.getValue());
        }
        return hash;
    }

    private static int valueHashCode(Object value) {
        if (value instanceof Object[]) {
            int h = 0;
            for (Object o : (Object[]) value) {
                h += valueHashCode(o);
            }
            return h;
        }
        return value == null ? 0 : value.hashCode();
    }

    private String toStringImpl() {
        StringBuilder b = new StringBuilder("@").append(type.getName()).append('(');
        boolean first = true;
        for (Map.Entry<String, Object> e : memberValues.entrySet()) {
            if (!first) {
                b.append(", ");
            }
            first = false;
            b.append(e.getKey()).append('=').append(e.getValue());
        }
        return b.append(')').toString();
    }
}
