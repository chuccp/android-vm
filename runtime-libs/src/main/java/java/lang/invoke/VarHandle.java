package java.lang.invoke;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

/**
 * 裁剪实现（java.lang.invoke 家族修复；docs §31.7 复杂类永远 lib）。
 *
 * 原版 VarHandle（VarForm/LambdaForm 形态链 + X-VarHandle 模板生成的
 * FieldInstance 族/FieldStatic 族/Array 族）依赖被后置的 JLI 引擎；本类按
 * 已消费面语义重写：
 *   - 字段/数组元素标识（refc/name/type/kind + 反射 Field 镜像）；
 *   - 31 个 @PolymorphicSignature accessors（erasure 形与原版一致，javac
 *     编出 invoke-polymorphic 调用点 → 解释器转 VarHandleGuards.guard_* 形）；
 *   - access(int mode, Object... coords) 语义核心：字段读写转发
 *     （guard_* 按字段读写语义转发到此）；volatile/acquire/release/opaque
 *     在本 VM（单执行流解释器）等价 plain（内存序语义后置并注记）。
 */
public class VarHandle {

    // kind（VM dispatch / access 语义分支）
    public static final int KIND_INSTANCE_FIELD = 0;
    public static final int KIND_STATIC_FIELD = 1;
    public static final int KIND_ARRAY_ELEMENT = 2;

    // VM/消费面标识（按字段名取；勿改名）
    final Class<?> refc;
    final String name;
    final Class<?> type;
    final int kind;
    final java.lang.reflect.Field field;  // 数组元素句柄为 null

    VarHandle(Class<?> refc, String name, Class<?> type, int kind,
              java.lang.reflect.Field field) {
        this.refc = Objects.requireNonNull(refc, "null refc");
        this.name = Objects.requireNonNull(name, "null name");
        this.type = Objects.requireNonNull(type, "null type");
        this.kind = kind;
        this.field = field;
    }

    // ---- AccessMode / AccessDescriptor（d8 脱糖形状消费面） ----

    public enum AccessMode {
        GET("get", AccessType.GET),
        SET("set", AccessType.SET),
        GET_VOLATILE("getVolatile", AccessType.GET),
        SET_VOLATILE("setVolatile", AccessType.SET),
        GET_ACQUIRE("getAcquire", AccessType.GET),
        SET_RELEASE("setRelease", AccessType.SET),
        GET_OPAQUE("getOpaque", AccessType.GET),
        SET_OPAQUE("setOpaque", AccessType.SET),
        COMPARE_AND_SET("compareAndSet", AccessType.COMPARE_AND_SET),
        COMPARE_AND_EXCHANGE("compareAndExchange", AccessType.COMPARE_AND_EXCHANGE),
        COMPARE_AND_EXCHANGE_ACQUIRE("compareAndExchangeAcquire", AccessType.COMPARE_AND_EXCHANGE),
        COMPARE_AND_EXCHANGE_RELEASE("compareAndExchangeRelease", AccessType.COMPARE_AND_EXCHANGE),
        WEAK_COMPARE_AND_SET_PLAIN("weakCompareAndSetPlain", AccessType.COMPARE_AND_SET),
        WEAK_COMPARE_AND_SET("weakCompareAndSet", AccessType.COMPARE_AND_SET),
        WEAK_COMPARE_AND_SET_ACQUIRE("weakCompareAndSetAcquire", AccessType.COMPARE_AND_SET),
        WEAK_COMPARE_AND_SET_RELEASE("weakCompareAndSetRelease", AccessType.COMPARE_AND_SET),
        GET_AND_SET("getAndSet", AccessType.GET_AND_UPDATE),
        GET_AND_SET_ACQUIRE("getAndSetAcquire", AccessType.GET_AND_UPDATE),
        GET_AND_SET_RELEASE("getAndSetRelease", AccessType.GET_AND_UPDATE),
        GET_AND_ADD("getAndAdd", AccessType.GET_AND_UPDATE),
        GET_AND_ADD_ACQUIRE("getAndAddAcquire", AccessType.GET_AND_UPDATE),
        GET_AND_ADD_RELEASE("getAndAddRelease", AccessType.GET_AND_UPDATE),
        GET_AND_BITWISE_OR("getAndBitwiseOr", AccessType.GET_AND_UPDATE),
        GET_AND_BITWISE_OR_RELEASE("getAndBitwiseOrRelease", AccessType.GET_AND_UPDATE),
        GET_AND_BITWISE_OR_ACQUIRE("getAndBitwiseOrAcquire", AccessType.GET_AND_UPDATE),
        GET_AND_BITWISE_AND("getAndBitwiseAnd", AccessType.GET_AND_UPDATE),
        GET_AND_BITWISE_AND_RELEASE("getAndBitwiseAndRelease", AccessType.GET_AND_UPDATE),
        GET_AND_BITWISE_AND_ACQUIRE("getAndBitwiseAndAcquire", AccessType.GET_AND_UPDATE),
        GET_AND_BITWISE_XOR("getAndBitwiseXor", AccessType.GET_AND_UPDATE),
        GET_AND_BITWISE_XOR_RELEASE("getAndBitwiseXorRelease", AccessType.GET_AND_UPDATE),
        GET_AND_BITWISE_XOR_ACQUIRE("getAndBitwiseXorAcquire", AccessType.GET_AND_UPDATE),
        ;

        static final Map<String, AccessMode> methodNameToAccessMode;
        static {
            AccessMode[] values = AccessMode.values();
            methodNameToAccessMode = new HashMap<>(values.length * 2);
            for (AccessMode am : values) {
                methodNameToAccessMode.put(am.methodName, am);
            }
        }

        final String methodName;
        final AccessType at;

        AccessMode(String methodName, AccessType at) {
            this.methodName = methodName;
            this.at = at;
        }

        public String methodName() { return methodName; }

        public static AccessMode valueFromMethodName(String methodName) {
            AccessMode am = methodNameToAccessMode.get(methodName);
            if (am != null) return am;
            throw new IllegalArgumentException("No AccessMode value for method name " + methodName);
        }
    }

    public enum AccessType {
        GET(Object.class),
        SET(void.class),
        COMPARE_AND_SET(boolean.class),
        COMPARE_AND_EXCHANGE(Object.class),
        GET_AND_UPDATE(Object.class);

        public final Class<?> returnType;
        public final boolean isMonomorphicInReturnType;

        AccessType(Class<?> returnType) {
            this.returnType = returnType;
            this.isMonomorphicInReturnType = returnType != Object.class;
        }
    }

    public static final class AccessDescriptor {
        public final MethodType symbolicMethodTypeErased;
        public final MethodType symbolicMethodTypeInvoker;
        public final Class<?> returnType;
        public final int type;
        public final int mode;

        public AccessDescriptor(MethodType symbolicMethodType, int type, int mode) {
            MethodType t = Objects.requireNonNull(symbolicMethodType, "null symbolicMethodType");
            this.symbolicMethodTypeErased = t;
            this.symbolicMethodTypeInvoker = t.insertParameterTypes(0, VarHandle.class);
            this.returnType = t.returnType();
            this.type = type;
            this.mode = mode;
        }

        /**
         * VM 解释器构造面（core/interpreter.cc invoke-polymorphic → guard_* 形）：
         * 按 accessor 名构造（symbolic type 以 returnType 退化表达）。
         */
        public static AccessDescriptor of(String methodName, Class<?> returnType) {
            AccessMode am = AccessMode.valueFromMethodName(methodName);
            MethodType symbolic = MethodType.methodType(
                    returnType == null ? void.class : returnType);
            return new AccessDescriptor(symbolic, 0, am.ordinal());
        }
    }

    // ---- signature-polymorphic accessors（erasure 形与原版一致） ----

    public final native @MethodHandle.PolymorphicSignature Object get(Object... args);
    public final native @MethodHandle.PolymorphicSignature void set(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getVolatile(Object... args);
    public final native @MethodHandle.PolymorphicSignature void setVolatile(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getOpaque(Object... args);
    public final native @MethodHandle.PolymorphicSignature void setOpaque(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAcquire(Object... args);
    public final native @MethodHandle.PolymorphicSignature void setRelease(Object... args);

    public final native @MethodHandle.PolymorphicSignature boolean compareAndSet(Object... args);

    public final native @MethodHandle.PolymorphicSignature Object compareAndExchange(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object compareAndExchangeAcquire(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object compareAndExchangeRelease(Object... args);

    public final native @MethodHandle.PolymorphicSignature boolean weakCompareAndSetPlain(Object... args);
    public final native @MethodHandle.PolymorphicSignature boolean weakCompareAndSet(Object... args);
    public final native @MethodHandle.PolymorphicSignature boolean weakCompareAndSetAcquire(Object... args);
    public final native @MethodHandle.PolymorphicSignature boolean weakCompareAndSetRelease(Object... args);

    public final native @MethodHandle.PolymorphicSignature Object getAndSet(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndSetAcquire(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndSetRelease(Object... args);

    public final native @MethodHandle.PolymorphicSignature Object getAndAdd(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndAddAcquire(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndAddRelease(Object... args);

    public final native @MethodHandle.PolymorphicSignature Object getAndBitwiseOr(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndBitwiseOrAcquire(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndBitwiseOrRelease(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndBitwiseAnd(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndBitwiseAndAcquire(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndBitwiseAndRelease(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndBitwiseXor(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndBitwiseXorAcquire(Object... args);
    public final native @MethodHandle.PolymorphicSignature Object getAndBitwiseXorRelease(Object... args);

    // ---- 内存栅栏（单执行流解释器：空实现即语义；内存序后置注记） ----

    public static void fullFence() { }
    public static void acquireFence() { }
    public static void releaseFence() { }
    public static void storeStoreFence() { }

    // ---- 视图 ----

    public final Class<?> varType() { return type; }

    public final List<Class<?>> coordinateTypes() {
        switch (kind) {
            case KIND_STATIC_FIELD:
                return Collections.emptyList();
            case KIND_ARRAY_ELEMENT: {
                List<Class<?>> ps = new ArrayList<>(2);
                ps.add(refc);
                ps.add(int.class);
                return ps;
            }
            default: {
                List<Class<?>> ps = new ArrayList<>(1);
                ps.add(refc);
                return ps;
            }
        }
    }

    public final MethodType accessModeType(AccessMode accessMode) {
        List<Class<?>> coords = coordinateTypes();
        Class<?>[] ps;
        switch (accessMode.at) {
            case GET: {
                ps = coords.toArray(new Class<?>[0]);
                return MethodType.methodType(type, ps);
            }
            case SET: {
                ps = new Class<?>[coords.size() + 1];
                coords.toArray(ps);
                ps[coords.size()] = type;
                return MethodType.methodType(void.class, ps);
            }
            case COMPARE_AND_SET: {
                ps = new Class<?>[coords.size() + 2];
                coords.toArray(ps);
                ps[coords.size()] = type;
                ps[coords.size() + 1] = type;
                return MethodType.methodType(boolean.class, ps);
            }
            case COMPARE_AND_EXCHANGE:
            case GET_AND_UPDATE: {
                ps = new Class<?>[coords.size() + 2];
                coords.toArray(ps);
                ps[coords.size()] = type;
                ps[coords.size() + 1] = type;
                return MethodType.methodType(type, ps);
            }
            default:
                throw new InternalError("Unknown AccessType");
        }
    }

    public final boolean isAccessModeSupported(AccessMode accessMode) {
        return accessMode != null;
    }

    public final MethodHandle toMethodHandle(AccessMode accessMode) {
        throw new UnsupportedOperationException("toMethodHandle (JLI 引擎) is postponed");
    }

    @Override
    public String toString() {
        return "VarHandle[" + refc.getName() + "." + name + " : " + type.getName()
                + ", kind=" + kind + "]";
    }

    // ==== 语义核心：guard_* 按字段读写语义转发到这里 ====

    /**
     * 按 AccessMode 语义执行字段/数组元素读写。
     * coords：instance=[recv, (value…)]，static=[(value…)]，array=[arr, index, (value…)]。
     * 内存序（volatile/acquire/release/opaque）在本 VM 等价 plain（解释器单执行流，
     * 内存序语义随协程/多执行流后置）；weak CAS 无 spurious failure（弱语义超集）。
     */
    public final Object access(int mode, Object... coords) {
        AccessMode[] modes = AccessMode.values();
        if (mode < 0 || mode >= modes.length) {
            throw new IllegalArgumentException("bad access mode " + mode);
        }
        Objects.requireNonNull(coords, "null coords");
        Object recv = null;
        int index = -1;
        int off = 0;
        if (kind == KIND_INSTANCE_FIELD) {
            if (coords.length < 1) throw new IllegalArgumentException("no receiver");
            recv = coords[0];
            if (recv == null) throw new NullPointerException("VarHandle receiver");
            off = 1;
        } else if (kind == KIND_ARRAY_ELEMENT) {
            if (coords.length < 2) throw new IllegalArgumentException("no array/index");
            recv = coords[0];
            if (recv == null) throw new NullPointerException("VarHandle array");
            index = ((Number) coords[1]).intValue();
            off = 2;
        }

        switch (modes[mode]) {
            case GET:
            case GET_VOLATILE:
            case GET_ACQUIRE:
            case GET_OPAQUE:
                return readValue(recv, index);

            case SET:
            case SET_VOLATILE:
            case SET_RELEASE:
            case SET_OPAQUE:
                writeValue(recv, index, coerce(coords[off]));
                return null;

            case GET_AND_SET:
            case GET_AND_SET_ACQUIRE:
            case GET_AND_SET_RELEASE: {
                Object old = readValue(recv, index);
                writeValue(recv, index, coerce(coords[off]));
                return old;
            }

            case GET_AND_ADD:
            case GET_AND_ADD_ACQUIRE:
            case GET_AND_ADD_RELEASE: {
                Object old = readValue(recv, index);
                writeValue(recv, index, addValues(old, coords[off]));
                return old;
            }

            case GET_AND_BITWISE_OR:
            case GET_AND_BITWISE_OR_ACQUIRE:
            case GET_AND_BITWISE_OR_RELEASE: {
                Object old = readValue(recv, index);
                writeValue(recv, index, bitwise(old, coords[off], 0));
                return old;
            }
            case GET_AND_BITWISE_AND:
            case GET_AND_BITWISE_AND_ACQUIRE:
            case GET_AND_BITWISE_AND_RELEASE: {
                Object old = readValue(recv, index);
                writeValue(recv, index, bitwise(old, coords[off], 1));
                return old;
            }
            case GET_AND_BITWISE_XOR:
            case GET_AND_BITWISE_XOR_ACQUIRE:
            case GET_AND_BITWISE_XOR_RELEASE: {
                Object old = readValue(recv, index);
                writeValue(recv, index, bitwise(old, coords[off], 2));
                return old;
            }

            case COMPARE_AND_SET:
            case WEAK_COMPARE_AND_SET:
            case WEAK_COMPARE_AND_SET_PLAIN:
            case WEAK_COMPARE_AND_SET_ACQUIRE:
            case WEAK_COMPARE_AND_SET_RELEASE: {
                Object old = readValue(recv, index);
                if (!valueMatches(old, coords[off])) return Boolean.FALSE;
                writeValue(recv, index, coerce(coords[off + 1]));
                return Boolean.TRUE;
            }

            case COMPARE_AND_EXCHANGE:
            case COMPARE_AND_EXCHANGE_ACQUIRE:
            case COMPARE_AND_EXCHANGE_RELEASE: {
                Object old = readValue(recv, index);
                if (valueMatches(old, coords[off])) {
                    writeValue(recv, index, coerce(coords[off + 1]));
                }
                return old;
            }

            default:
                throw new InternalError("Unknown AccessMode " + modes[mode]);
        }
    }

    // ---- 读写底座 ----

    private Object readValue(Object recv, int index) {
        try {
            if (kind == KIND_ARRAY_ELEMENT) return arrayRead(recv, index);
            return field.get(kind == KIND_STATIC_FIELD ? null : recv);
        } catch (IllegalAccessException e) {
            throw new IllegalStateException("VarHandle read " + name, e);
        }
    }

    private void writeValue(Object recv, int index, Object value) {
        try {
            if (kind == KIND_ARRAY_ELEMENT) {
                arrayWrite(recv, index, value);
                return;
            }
            field.set(kind == KIND_STATIC_FIELD ? null : recv, value);
        } catch (IllegalAccessException e) {
            throw new IllegalStateException("VarHandle write " + name, e);
        }
    }

    private Object arrayRead(Object arr, int index) {
        if (arr instanceof Object[]) return ((Object[]) arr)[index];
        if (arr instanceof boolean[]) return Boolean.valueOf(((boolean[]) arr)[index]);
        if (arr instanceof byte[]) return Byte.valueOf(((byte[]) arr)[index]);
        if (arr instanceof char[]) return Character.valueOf(((char[]) arr)[index]);
        if (arr instanceof short[]) return Short.valueOf(((short[]) arr)[index]);
        if (arr instanceof int[]) return Integer.valueOf(((int[]) arr)[index]);
        if (arr instanceof long[]) return Long.valueOf(((long[]) arr)[index]);
        if (arr instanceof float[]) return Float.valueOf(((float[]) arr)[index]);
        if (arr instanceof double[]) return Double.valueOf(((double[]) arr)[index]);
        throw new IllegalArgumentException("not an array: " + refc);
    }

    private void arrayWrite(Object arr, int index, Object value) {
        if (arr instanceof Object[]) {
            ((Object[]) arr)[index] = value;
        } else if (arr instanceof boolean[]) {
            ((boolean[]) arr)[index] = ((Boolean) value).booleanValue();
        } else if (arr instanceof byte[]) {
            ((byte[]) arr)[index] = ((Number) value).byteValue();
        } else if (arr instanceof char[]) {
            ((char[]) arr)[index] = ((Character) value).charValue();
        } else if (arr instanceof short[]) {
            ((short[]) arr)[index] = ((Number) value).shortValue();
        } else if (arr instanceof int[]) {
            ((int[]) arr)[index] = ((Number) value).intValue();
        } else if (arr instanceof long[]) {
            ((long[]) arr)[index] = ((Number) value).longValue();
        } else if (arr instanceof float[]) {
            ((float[]) arr)[index] = ((Number) value).floatValue();
        } else if (arr instanceof double[]) {
            ((double[]) arr)[index] = ((Number) value).doubleValue();
        } else {
            throw new IllegalArgumentException("not an array: " + refc);
        }
    }

    /** 装箱值按字段类型收窄（guard_* 形的 Z/B/C/S 坐标按 I 传递）。 */
    private Object coerce(Object value) {
        if (value == null) {
            if (type.isPrimitive()) throw new NullPointerException("null for primitive " + type);
            return null;
        }
        if (type == boolean.class) {
            return value instanceof Boolean ? value
                    : Boolean.valueOf(((Number) value).intValue() != 0);
        }
        if (type == byte.class) {
            return value instanceof Byte ? value : Byte.valueOf(((Number) value).byteValue());
        }
        if (type == char.class) {
            return value instanceof Character ? value
                    : Character.valueOf((char) ((Number) value).intValue());
        }
        if (type == short.class) {
            return value instanceof Short ? value : Short.valueOf(((Number) value).shortValue());
        }
        if (type == int.class) {
            return value instanceof Integer ? value : Integer.valueOf(((Number) value).intValue());
        }
        if (type == long.class) {
            return value instanceof Long ? value : Long.valueOf(((Number) value).longValue());
        }
        if (type == float.class) {
            return value instanceof Float ? value : Float.valueOf(((Number) value).floatValue());
        }
        if (type == double.class) {
            return value instanceof Double ? value : Double.valueOf(((Number) value).doubleValue());
        }
        return value;
    }

    /** 原子读改写语义（getAndAdd）：old + delta 按字段类型。 */
    private Object addValues(Object old, Object delta) {
        Object d = coerce(delta);
        if (type == int.class) {
            return Integer.valueOf(((Number) old).intValue() + ((Number) d).intValue());
        }
        if (type == long.class) {
            return Long.valueOf(((Number) old).longValue() + ((Number) d).longValue());
        }
        if (type == float.class) {
            return Float.valueOf(((Number) old).floatValue() + ((Number) d).floatValue());
        }
        if (type == double.class) {
            return Double.valueOf(((Number) old).doubleValue() + ((Number) d).doubleValue());
        }
        if (type == byte.class) {
            return Byte.valueOf((byte) (((Number) old).byteValue() + ((Number) d).byteValue()));
        }
        if (type == short.class) {
            return Short.valueOf((short) (((Number) old).shortValue() + ((Number) d).shortValue()));
        }
        if (type == char.class) {
            return Character.valueOf((char) (((Character) old).charValue()
                    + ((Number) d).intValue()));
        }
        throw new UnsupportedOperationException("getAndAdd on " + type);
    }

    /** getAndBitwise*：old OP value；op：0=OR 1=AND 2=XOR。 */
    private Object bitwise(Object old, Object value, int op) {
        Object v = coerce(value);
        if (type == int.class) {
            int a = ((Number) old).intValue();
            int b = ((Number) v).intValue();
            return Integer.valueOf(op == 0 ? (a | b) : op == 1 ? (a & b) : (a ^ b));
        }
        if (type == long.class) {
            long a = ((Number) old).longValue();
            long b = ((Number) v).longValue();
            return Long.valueOf(op == 0 ? (a | b) : op == 1 ? (a & b) : (a ^ b));
        }
        if (type == boolean.class) {
            boolean a = ((Boolean) old).booleanValue();
            boolean b = ((Boolean) v).booleanValue();
            return Boolean.valueOf(op == 0 ? (a | b) : op == 1 ? (a & b) : (a ^ b));
        }
        if (type == byte.class) {
            byte a = ((Number) old).byteValue();
            byte b = ((Number) v).byteValue();
            return Byte.valueOf((byte) (op == 0 ? (a | b) : op == 1 ? (a & b) : (a ^ b)));
        }
        if (type == short.class) {
            short a = ((Number) old).shortValue();
            short b = ((Number) v).shortValue();
            return Short.valueOf((short) (op == 0 ? (a | b) : op == 1 ? (a & b) : (a ^ b)));
        }
        if (type == char.class) {
            char a = ((Character) old).charValue();
            char b = ((Character) v).charValue();
            return Character.valueOf((char) (op == 0 ? (a | b) : op == 1 ? (a & b) : (a ^ b)));
        }
        throw new UnsupportedOperationException("getAndBitwise* on " + type);
    }

    /** CAS 匹配：基本类型按值（装箱 equals），引用类型按 ==。 */
    private boolean valueMatches(Object current, Object expected) {
        if (type.isPrimitive()) {
            Object e = coerce(expected);
            return current == null ? e == null : current.equals(e);
        }
        return current == expected;
    }
}
