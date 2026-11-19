package java.lang.invoke;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

/**
 * 裁剪实现（java.lang.invoke 家族修复；docs §31.7 复杂类永远 lib）。
 *
 * 原版 MethodType/MethodTypeForm 依赖 LambdaForm/Invokers/MH 引擎链
 * （sun.invoke.util.* 未 vendored、版本错配编不过/宿主泄漏），引擎整体后置，
 * 本类按消费面语义重写：描述符驱动的参数/返回类型视图 + 常用工厂。
 * 运行时消费面：Lookup.findVirtual/findStatic、MethodHandle.type()、
 * MemberName/AccessDescriptor 形状、StackWalker.getMethodType()。
 */
public final class MethodType implements java.io.Serializable {
    private static final long serialVersionUID = 292L;

    private final Class<?> rtype;
    private final Class<?>[] ptypes;
    private final String descriptor;

    private MethodType(Class<?> rtype, Class<?>[] ptypes) {
        this.rtype = rtype;
        this.ptypes = ptypes;
        this.descriptor = BuildDescriptor(rtype, ptypes);
    }

    private MethodType(String descriptor, Class<?> rtype, Class<?>[] ptypes) {
        this.rtype = rtype;
        this.ptypes = ptypes;
        this.descriptor = descriptor;
    }

    // ---- 工厂 ----

    /** JDK 单参形（fastjson2 等按 (Class;) 描述符直呼）。 */
    public static MethodType methodType(Class<?> rtype) {
        return methodType(rtype, new Class<?>[0]);
    }

    /** JDK 双参形（非 varargs 描述符 (Class;Class;)）。 */
    public static MethodType methodType(Class<?> rtype, Class<?> ptype0) {
        return methodType(rtype, new Class<?>[] { ptype0 });
    }

    /** JDK 三参形：首个参数类型单独给出（fastjson2 等按该描述符直呼）。 */
    public static MethodType methodType(Class<?> rtype, Class<?> ptype0, Class<?>... ptypes) {
        Class<?>[] all = new Class<?>[ptypes.length + 1];
        all[0] = ptype0;
        System.arraycopy(ptypes, 0, all, 1, ptypes.length);
        return methodType(rtype, all);
    }

    public static MethodType methodType(Class<?> rtype, Class<?>... ptypes) {
        Objects.requireNonNull(rtype, "null return type");
        Objects.requireNonNull(ptypes, "null parameter types");
        return new MethodType(rtype, ptypes.clone());
    }

    public static MethodType methodType(Class<?> rtype, List<Class<?>> ptypes) {
        Objects.requireNonNull(rtype, "null return type");
        Objects.requireNonNull(ptypes, "null parameter types");
        return new MethodType(rtype, ptypes.toArray(new Class<?>[0]));
    }

    public static MethodType methodType(Class<?> rtype, MethodType ptypes) {
        Objects.requireNonNull(rtype, "null return type");
        Objects.requireNonNull(ptypes, "null parameter types");
        return new MethodType(rtype, ptypes.ptypes.clone());
    }

    /** 从 JVM 描述符构造（如 "(IJ)Ljava/lang/String;"）。 */
    public static MethodType fromDescriptor(String descriptor, ClassLoader loader)
            throws IllegalArgumentException, TypeNotPresentException {
        Objects.requireNonNull(descriptor, "null descriptor");
        int close = descriptor.indexOf(')');
        if (descriptor.isEmpty() || descriptor.charAt(0) != '(' || close < 0) {
            throw new IllegalArgumentException("bad descriptor: " + descriptor);
        }
        List<Class<?>> ps = new ArrayList<>();
        int i = 1;
        while (i < close) {
            int next = ParseType(descriptor, i, loader, ps);
            i = next;
        }
        Class<?>[] parr = ps.toArray(new Class<?>[0]);
        Class<?> r = ParseOne(descriptor, close + 1, loader);
        return new MethodType(descriptor, r, parr);
    }

    // ---- 视图 ----

    public Class<?> returnType() { return rtype; }

    public List<Class<?>> parameterList() {
        return Collections.unmodifiableList(Arrays.asList(ptypes));
    }

    public Class<?>[] parameterArray() { return ptypes.clone(); }

    public int parameterCount() { return ptypes.length; }

    /** 包内快速访问（不拷贝）。 */
    Class<?>[] ptypes() { return ptypes; }

    /** JVM 描述符（"([IJ)V" 形态）。 */
    public String descriptorString() { return descriptor; }

    // ---- 变换 ----

    public MethodType changeReturnType(Class<?> nrtype) {
        return methodType(nrtype, this);
    }

    public MethodType appendParameterTypes(Class<?>... ptypesToInsert) {
        Class<?>[] all = new Class<?>[ptypes.length + ptypesToInsert.length];
        System.arraycopy(ptypes, 0, all, 0, ptypes.length);
        System.arraycopy(ptypesToInsert, 0, all, ptypes.length, ptypesToInsert.length);
        return methodType(rtype, all);
    }

    public MethodType insertParameterTypes(int num, Class<?>... ptypesToInsert) {
        if (num < 0 || num > ptypes.length) throw new IllegalArgumentException("bad index " + num);
        Class<?>[] all = new Class<?>[ptypes.length + ptypesToInsert.length];
        System.arraycopy(ptypes, 0, all, 0, num);
        System.arraycopy(ptypesToInsert, 0, all, num, ptypesToInsert.length);
        System.arraycopy(ptypes, num, all, num + ptypesToInsert.length, ptypes.length - num);
        return methodType(rtype, all);
    }

    public MethodType dropParameterTypes(int start, int num) {
        if (start < 0 || num < 0 || start + num > ptypes.length) {
            throw new IllegalArgumentException("bad range");
        }
        Class<?>[] all = new Class<?>[ptypes.length - num];
        System.arraycopy(ptypes, 0, all, 0, start);
        System.arraycopy(ptypes, start + num, all, start, ptypes.length - start - num);
        return methodType(rtype, all);
    }

    // ---- Object ----

    @Override
    public boolean equals(Object other) {
        return other instanceof MethodType && descriptor.equals(((MethodType) other).descriptor);
    }

    @Override
    public int hashCode() { return descriptor.hashCode(); }

    @Override
    public String toString() { return descriptor; }

    // ---- 描述符工具 ----

    private static String BuildDescriptor(Class<?> rtype, Class<?>[] ptypes) {
        StringBuilder sb = new StringBuilder("(");
        for (Class<?> p : ptypes) sb.append(TypeDescriptor(p));
        sb.append(')').append(TypeDescriptor(rtype));
        return sb.toString();
    }

    static String TypeDescriptor(Class<?> type) {
        if (type == void.class) return "V";
        if (type == int.class) return "I";
        if (type == long.class) return "J";
        if (type == float.class) return "F";
        if (type == double.class) return "D";
        if (type == boolean.class) return "Z";
        if (type == byte.class) return "B";
        if (type == char.class) return "C";
        if (type == short.class) return "S";
        if (type.isArray()) return "[" + TypeDescriptor(type.getComponentType());
        return "L" + type.getName().replace('.', '/') + ";";
    }

    private static Class<?> ParseOne(String d, int at, ClassLoader loader) {
        List<Class<?>> tmp = new ArrayList<>(1);
        ParseType(d, at, loader, tmp);
        return tmp.get(0);
    }

    /** 解析 at 处类型并返回下一位置；out 非空则追加。 */
    private static int ParseType(String d, int at, ClassLoader loader, List<Class<?>> out) {
        char c = d.charAt(at);
        switch (c) {
            case 'V':
                if (out != null) out.add(void.class);
                return at + 1;
            case 'I':
                if (out != null) out.add(int.class);
                return at + 1;
            case 'J':
                if (out != null) out.add(long.class);
                return at + 1;
            case 'F':
                if (out != null) out.add(float.class);
                return at + 1;
            case 'D':
                if (out != null) out.add(double.class);
                return at + 1;
            case 'Z':
                if (out != null) out.add(boolean.class);
                return at + 1;
            case 'B':
                if (out != null) out.add(byte.class);
                return at + 1;
            case 'C':
                if (out != null) out.add(char.class);
                return at + 1;
            case 'S':
                if (out != null) out.add(short.class);
                return at + 1;
            case '[': {
                int next = ParseType(d, at + 1, loader, null);
                Class<?> comp = ParseOne(d, at + 1, loader);
                if (out != null) out.add(java.lang.reflect.Array.newInstance(comp, 0).getClass());
                return next;
            }
            case 'L': {
                int end = d.indexOf(';', at);
                if (end < 0) throw new IllegalArgumentException("bad descriptor: " + d);
                String name = d.substring(at + 1, end).replace('/', '.');
                if (out != null) out.add(ClassResolve(name, loader));
                return end + 1;
            }
            default:
                throw new IllegalArgumentException("bad descriptor at " + at + ": " + d);
        }
    }

    private static Class<?> ClassResolve(String name, ClassLoader loader) {
        try {
            if (loader != null) return Class.forName(name, false, loader);
            return Class.forName(name);
        } catch (ClassNotFoundException e) {
            throw new TypeNotPresentException(name, e);
        }
    }
}
