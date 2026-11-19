package java.lang.invoke;

import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.Objects;

/**
 * 裁剪实现（java.lang.invoke 家族修复；docs §31.7 复杂类永远 lib）。
 *
 * 原版 MethodHandles（2069 行：IMPL_NAMES/MemberName.Factory 解析引擎 +
 * VerifyAccess 模块封装）依赖链不可闭合（VerifyAccess 未 vendored、引擎后置），
 * 按已消费面重写：
 *   - lookup()/publicLookup()/privateLookupIn（VM 无模块封装语义）
 *   - Lookup.findVarHandle/findStaticVarHandle（字段 VarHandle 语义）
 *   - Lookup.findVirtual/findStatic/findSpecial（MethodHandle marker 面目标）
 *   - arrayElementVarHandle（juc Atomic*Array/Exchanger 等消费面）
 * findGetter/findSetter/反射 unreflect* 等引擎面后置（注记）。
 * 注：VM 无 caller-sensitive 语义（jdk.internal.reflect.Reflection.getCallerClass
 * 恒 null），lookup() 的 lookupClass 退化为 Object.class（访问检查不依赖它）。
 */
public class MethodHandles {

    static {
        // 注册 JavaLangInvokeAccess（StackFrameInfo.getMethodType 消费面）
        MethodHandleNatives.ensureInitialized();
    }

    private MethodHandles() { }

    public static Lookup lookup() {
        Class<?> caller = jdk.internal.reflect.Reflection.getCallerClass();
        return new Lookup(caller != null ? caller : Object.class);
    }

    public static Lookup publicLookup() {
        return Lookup.PUBLIC_LOOKUP;
    }

    /** VM 无模块封装：恒授予全权 Lookup（语义保真点 docs 14 章 §31 检查单）。 */
    public static Lookup privateLookupIn(Class<?> targetClass, Lookup lookup)
            throws IllegalAccessException {
        Objects.requireNonNull(targetClass, "null targetClass");
        Objects.requireNonNull(lookup, "null lookup");
        if (targetClass.isPrimitive()) {
            throw new IllegalArgumentException(targetClass + " is a primitive class");
        }
        return new Lookup(targetClass);
    }

    public static VarHandle arrayElementVarHandle(Class<?> arrayClass) {
        Objects.requireNonNull(arrayClass, "null arrayClass");
        if (!arrayClass.isArray()) {
            throw new IllegalArgumentException("not an array class: " + arrayClass);
        }
        return new VarHandle(arrayClass, "[]", arrayClass.getComponentType(),
                VarHandle.KIND_ARRAY_ELEMENT, null);
    }

    // 引擎面（constant/identity/dropArguments…）：后置；按需以 UnsupportedOperationException
    // 占位显式表达缺口（防静默错义）。
    public static MethodHandle identity(Class<?> type) {
        throw new UnsupportedOperationException("MethodHandles.identity (JLI 引擎) is postponed");
    }

    public static MethodHandle constant(Class<?> type, Object value) {
        throw new UnsupportedOperationException("MethodHandles.constant (JLI 引擎) is postponed");
    }

    public static MethodHandle empty(MethodType type) {
        throw new UnsupportedOperationException("MethodHandles.empty (JLI 引擎) is postponed");
    }

    // ==== Lookup ====

    public static final class Lookup {
        public static final int PUBLIC = 0x00000001;
        public static final int PRIVATE = 0x00000002;
        public static final int PROTECTED = 0x00000004;
        public static final int PACKAGE = 0x00000008;
        public static final int ALL_MODES = PUBLIC | PRIVATE | PROTECTED | PACKAGE;

        static final Lookup PUBLIC_LOOKUP = new Lookup(Object.class, PUBLIC);

        private final Class<?> lookupClass;
        private final int allowedModes;

        Lookup(Class<?> lookupClass) {
            this(lookupClass, ALL_MODES);
        }

        private Lookup(Class<?> lookupClass, int allowedModes) {
            this.lookupClass = Objects.requireNonNull(lookupClass, "null lookupClass");
            this.allowedModes = allowedModes;
        }

        public Class<?> lookupClass() { return lookupClass; }

        public int lookupModes() { return allowedModes; }

        public Lookup in(Class<?> requestedLookupClass) {
            Objects.requireNonNull(requestedLookupClass, "null requestedLookupClass");
            return new Lookup(requestedLookupClass, allowedModes);
        }

        // ---- VarHandle 面 ----

        /**
         * 实例字段 VarHandle（语义：字段读写/ CAS 族经 VarHandle.access）。
         * VM 反射面按类自身声明查找——超类字段由本方法沿继承链回退。
         */
        public VarHandle findVarHandle(Class<?> refc, String name, Class<?> type)
                throws NoSuchFieldException {
            Field f = LookupFieldChain(refc, name);
            if (f == null) {
                throw new NoSuchFieldException(refc.getName() + "." + name);
            }
            if (Modifier.isStatic(f.getModifiers())) {
                throw new NoSuchFieldException("static field: " + refc.getName() + "." + name);
            }
            if (f.getType() != type) {
                throw new NoSuchFieldException("field type mismatch: " + refc.getName() + "." + name
                        + " is " + f.getType().getName() + ", not " + type.getName());
            }
            f.setAccessible(true);
            return new VarHandle(refc, name, type, VarHandle.KIND_INSTANCE_FIELD, f);
        }

        public VarHandle findStaticVarHandle(Class<?> refc, String name, Class<?> type)
                throws NoSuchFieldException {
            Field f = LookupFieldChain(refc, name);
            if (f == null) {
                throw new NoSuchFieldException(refc.getName() + "." + name);
            }
            if (!Modifier.isStatic(f.getModifiers())) {
                throw new NoSuchFieldException("instance field: " + refc.getName() + "." + name);
            }
            if (f.getType() != type) {
                throw new NoSuchFieldException("field type mismatch: " + refc.getName() + "." + name
                        + " is " + f.getType().getName() + ", not " + type.getName());
            }
            f.setAccessible(true);
            return new VarHandle(refc, name, type, VarHandle.KIND_STATIC_FIELD, f);
        }

        public VarHandle unreflectVarHandle(Field f) throws IllegalAccessException {
            Objects.requireNonNull(f, "null field");
            if (f.isAccessible()) {
                // setAccessible 已授予
            } else if (!Modifier.isPublic(f.getModifiers())
                    || !Modifier.isPublic(f.getDeclaringClass().getModifiers())) {
                throw new IllegalAccessException(f.toString());
            }
            f.setAccessible(true);
            int kind = Modifier.isStatic(f.getModifiers())
                    ? VarHandle.KIND_STATIC_FIELD : VarHandle.KIND_INSTANCE_FIELD;
            return new VarHandle(f.getDeclaringClass(), f.getName(), f.getType(), kind, f);
        }

        // ---- MethodHandle 面（marker 目标） ----

        public MethodHandle findVirtual(Class<?> refc, String name, MethodType type)
                throws NoSuchMethodException, IllegalAccessException {
            return FindMethod(refc, name, type, MemberName.REF_invokeVirtual);
        }

        public MethodHandle findStatic(Class<?> refc, String name, MethodType type)
                throws NoSuchMethodException, IllegalAccessException {
            return FindMethod(refc, name, type, MemberName.REF_invokeStatic);
        }

        public MethodHandle findSpecial(Class<?> refc, String name, MethodType type,
                Class<?> specialCaller) throws NoSuchMethodException, IllegalAccessException {
            Objects.requireNonNull(specialCaller, "null specialCaller");
            return FindMethod(refc, name, type, MemberName.REF_invokeSpecial);
        }

        private MethodHandle FindMethod(Class<?> refc, String name, MethodType type, int refKind)
                throws NoSuchMethodException {
            Objects.requireNonNull(refc, "null refc");
            Objects.requireNonNull(name, "null name");
            Objects.requireNonNull(type, "null type");
            Class<?>[] ptypes = MethodHandle.MethodHandleDispatch.ParameterTypes(
                    type.descriptorString());
            java.lang.reflect.Method m = FindReflectMethod(refc, name, ptypes);
            boolean isStatic = Modifier.isStatic(m.getModifiers());
            if (refKind == MemberName.REF_invokeStatic && !isStatic) {
                throw new NoSuchMethodException("not static: " + refc.getName() + "." + name);
            }
            if (refKind != MemberName.REF_invokeStatic && isStatic) {
                throw new NoSuchMethodException("static method: " + refc.getName() + "." + name);
            }
            if (m.getReturnType() != type.returnType()) {
                throw new NoSuchMethodException("return type mismatch: " + refc.getName() + "." + name);
            }
            m.setAccessible(true);
            MemberName mn = new MemberName(refc, name, type.descriptorString(), refKind);
            return new MethodHandle.Direct(mn, type);
        }

        private static java.lang.reflect.Method FindReflectMethod(Class<?> cls, String name,
                Class<?>[] ptypes) throws NoSuchMethodException {
            for (Class<?> c = cls; c != null; c = c.getSuperclass()) {
                try {
                    return c.getDeclaredMethod(name, ptypes);
                } catch (NoSuchMethodException ignored) {
                }
            }
            for (Class<?> iface : cls.getInterfaces()) {
                try {
                    return FindReflectMethod(iface, name, ptypes);
                } catch (NoSuchMethodException ignored) {
                }
            }
            throw new NoSuchMethodException(cls.getName() + "." + name);
        }

        private static Field LookupFieldChain(Class<?> refc, String name) {
            Objects.requireNonNull(refc, "null refc");
            Objects.requireNonNull(name, "null name");
            for (Class<?> c = refc; c != null; c = c.getSuperclass()) {
                try {
                    return c.getDeclaredField(name);
                } catch (NoSuchFieldException ignored) {
                }
            }
            return null;
        }
    }
}
