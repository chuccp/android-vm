package java.lang.invoke;

import java.util.Arrays;
import java.util.List;
import java.util.Objects;

/**
 * 裁剪实现（java.lang.invoke 家族修复；docs §31.7 复杂类永远 lib）。
 *
 * 原版 MethodHandle（LambdaForm/DirectMethodHandle/BoundMethodHandle/MemberName
 * 解析引擎）整体后置；本类保留消费面：
 *   1. signature-polymorphic marker 面（invoke/invokeExact/invokeBasic/linkTo*）——
 *      javac 按 @PolymorphicSignature+native 规约编出 invoke-polymorphic 调用点，
 *      解释器（core/interpreter.cc）提供基础派发语义（对 MethodHandle 目标）；
 *   2. MemberName 目标标识（declaring/name/descriptor/refKind——VM 按字段名读取）；
 *   3. invokeWithArguments/type()/asType 最小语义。
 * LambdaForm 生成机明确后置（docs 14 章 §31.7 检查单）。
 */
public abstract class MethodHandle {

    /** javac signature-polymorphic 判定标记（JLS 15.12.3：owner+native+Object...）。 */
    @interface PolymorphicSignature { }

    // VM dispatch 读取面（按字段名取；勿改名）
    final MemberName member;
    final MethodType type;

    protected MethodHandle(MemberName member, MethodType type) {
        this.member = Objects.requireNonNull(member, "null member");
        this.type = Objects.requireNonNull(type, "null type");
    }

    public MethodType type() { return type; }

    public MemberName internalMember() { return member; }

    // ---- signature-polymorphic marker 面 ----

    public final native
    @PolymorphicSignature
    Object invokeExact(Object... args) throws Throwable;

    public final native
    @PolymorphicSignature
    Object invoke(Object... args) throws Throwable;

    public final native
    @PolymorphicSignature
    Object invokeBasic(Object... args) throws Throwable;

    public static native
    @PolymorphicSignature
    Object linkToVirtual(Object... args) throws Throwable;

    public static native
    @PolymorphicSignature
    Object linkToStatic(Object... args) throws Throwable;

    public static native
    @PolymorphicSignature
    Object linkToSpecial(Object... args) throws Throwable;

    public static native
    @PolymorphicSignature
    Object linkToInterface(Object... args) throws Throwable;

    // ---- 常规语义面 ----

    /** 按参数列表派发（擦除形）：经 VM 目标方法直调（无 asType 转换链——后置）。 */
    public final Object invokeWithArguments(Object... arguments) throws Throwable {
        return MethodHandleDispatch.invokeMember(member, arguments == null ? new Object[0] : arguments);
    }

    public final Object invokeWithArguments(List<?> arguments) throws Throwable {
        Objects.requireNonNull(arguments, "null arguments");
        return invokeWithArguments(arguments.toArray());
    }

    public MethodHandle asType(MethodType newType) {
        if (newType == null) throw new NullPointerException("null newType");
        // 最小语义：仅允许描述符等价；类型转换链（asType 坐标转换）后置
        if (newType.descriptorString().equals(type.descriptorString())) return this;
        throw new UnsupportedOperationException("asType conversion is postponed: "
                + type + " -> " + newType);
    }

    public MethodHandle asFixedArity() { return this; }

    public MethodHandle bindTo(Object x) {
        throw new UnsupportedOperationException("bindTo is postponed");
    }

    @Override
    public String toString() {
        return getClass().getSimpleName() + "[" + member + " : " + type + "]";
    }

    // ---- 直柄（findVirtual/findStatic 产物） ----

    static final class Direct extends MethodHandle {
        Direct(MemberName member, MethodType type) {
            super(member, type);
        }
    }

    // ---- 擦除形派发（invokeWithArguments / 解释器回落面共用） ----

    /**
     * 包内反射派发：Marker 面的 Java 兜底（擦除形调用/测试面）。
     * VM 解释器对 invoke-polymorphic 形态走 core/interpreter.cc 直派；
     * 本方法按 MemberName 找 java.lang.reflect.Method 直调，语义一致。
     */
    static final class MethodHandleDispatch {
        static Object invokeMember(MemberName mn, Object[] args) throws Throwable {
            Class<?>[] ptypes = ParameterTypes(mn.descriptor);
            java.lang.reflect.Method m;
            try {
                m = FindMethod(mn.declaringClass, mn.name, ptypes);
            } catch (NoSuchMethodException e) {
                throw new NoSuchMethodError(mn.toString());
            }
            if (mn.isStatic()) {
                return m.invoke(null, args);
            }
            if (args.length == 0) throw new NullPointerException("no receiver");
            Object recv = args[0];
            Object[] rest = Arrays.copyOfRange(args, 1, args.length);
            return m.invoke(recv, rest);
        }

        private static java.lang.reflect.Method FindMethod(Class<?> cls, String name,
                Class<?>[] ptypes) throws NoSuchMethodException {
            for (Class<?> c = cls; c != null; c = c.getSuperclass()) {
                try {
                    java.lang.reflect.Method m = c.getDeclaredMethod(name, ptypes);
                    m.setAccessible(true);
                    return m;
                } catch (NoSuchMethodException ignored) {
                }
            }
            for (Class<?> iface : cls.getInterfaces()) {
                try {
                    return FindMethod(iface, name, ptypes);
                } catch (NoSuchMethodException ignored) {
                }
            }
            throw new NoSuchMethodException(cls.getName() + "." + name);
        }

        static Class<?>[] ParameterTypes(String descriptor) {
            int close = descriptor.indexOf(')');
            if (descriptor.isEmpty() || descriptor.charAt(0) != '(' || close < 0) {
                throw new IllegalArgumentException("bad descriptor: " + descriptor);
            }
            java.util.ArrayList<Class<?>> ps = new java.util.ArrayList<>();
            int i = 1;
            while (i < close) {
                i = AddType(descriptor, i, ps);
            }
            return ps.toArray(new Class<?>[0]);
        }

        private static int AddType(String d, int at, java.util.ArrayList<Class<?>> out) {
            char c = d.charAt(at);
            switch (c) {
                case 'I': out.add(int.class); return at + 1;
                case 'J': out.add(long.class); return at + 1;
                case 'F': out.add(float.class); return at + 1;
                case 'D': out.add(double.class); return at + 1;
                case 'Z': out.add(boolean.class); return at + 1;
                case 'B': out.add(byte.class); return at + 1;
                case 'C': out.add(char.class); return at + 1;
                case 'S': out.add(short.class); return at + 1;
                case '[': {
                    int next = AddType(d, at + 1, out);
                    Class<?> comp = out.remove(out.size() - 1);
                    out.add(java.lang.reflect.Array.newInstance(comp, 0).getClass());
                    return next;
                }
                case 'L': {
                    int end = d.indexOf(';', at);
                    if (end < 0) throw new IllegalArgumentException("bad descriptor: " + d);
                    String name = d.substring(at + 1, end).replace('/', '.');
                    try {
                        out.add(Class.forName(name));
                    } catch (ClassNotFoundException e) {
                        throw new TypeNotPresentException(name, e);
                    }
                    return end + 1;
                }
                default:
                    throw new IllegalArgumentException("bad descriptor at " + at + ": " + d);
            }
        }
    }
}
