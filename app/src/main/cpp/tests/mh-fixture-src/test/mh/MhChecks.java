package test.mh;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandleInfo;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.invoke.VarHandle;
import java.lang.invoke.VarHandleGuards;
import java.util.concurrent.ConcurrentLinkedQueue;

/**
 * java.lang.invoke 家族语义冒烟（tools/methodhandles_tests.sh 编译进
 * tests/out/mh-fixture.dex，min-api 26 → invoke-polymorphic 真 opcode 形）。
 *
 * 覆盖（本轮验收线）：
 *   1. MethodHandles.lookup() / Lookup.findVarHandle / findStaticVarHandle；
 *   2. VarHandle accessor 全 op 面（经解释器 → VarHandleGuards.guard_* 形）；
 *   3. guard_* 直调（形状矩阵 + mode 区分语义）；
 *   4. MethodHandle.invokeBasic/invokeExact/invoke/linkToStatic marker 面；
 *   5. CLQ offer/poll 全链（VarHandle 语义端到端）。
 * 失败即抛 AssertionError（消息 = 检查名），成功返回 ":ok"。
 */
public final class MhChecks {

    static class Box {
        int i;
        long j;
        Object o;
        boolean z;
        double d;

        int twice(int x) { return x * 2; }
    }

    static int sInt = 7;
    static long sLong = 8;
    static Object sObj;

    static int add(int a, int b) { return a + b; }

    private static int failures;

    private static void check(boolean cond, String what) {
        if (!cond) {
            failures++;
            throw new IllegalStateException(stage + "/" + what);
        }
    }

    static String stage = "";

    static void at(String s) { stage = s; }

    public static String runAll() {
        failures = 0;
        try {
            lookupAndFind();
            accessorOps();
            guardShapes();
            methodHandleMarkers();
            clqChain();
        } catch (Throwable t) {
            return "FAIL@" + stage + ": " + t;
        }
        return ":ok";
    }

    // ---- 1. lookup / findVarHandle ----

    static void lookupAndFind() {
        MethodHandles.Lookup l = MethodHandles.lookup();
        check(l != null, "lookup non-null");
        check(l.lookupClass() != null, "lookupClass non-null");
        try {
            VarHandle vi = l.findVarHandle(Box.class, "i", int.class);
            check(vi != null, "findVarHandle int");
            check(vi.varType() == int.class, "varType int");
            check(vi.coordinateTypes().size() == 1, "coordinateTypes instance=1");

            VarHandle vo = l.findVarHandle(Box.class, "o", Object.class);
            check(vo != null, "findVarHandle Object");

            // 超类字段（Box 的基类 Object 无字段；用字段链上抛验证不误报）
            VarHandle vj = l.findVarHandle(Box.class, "j", long.class);
            check(vj != null, "findVarHandle long");

            VarHandle vs = l.findStaticVarHandle(MhChecks.class, "sInt", int.class);
            check(vs != null, "findStaticVarHandle int");

            VarHandle arr = MethodHandles.arrayElementVarHandle(int[].class);
            check(arr != null, "arrayElementVarHandle int[]");
            check(arr.coordinateTypes().size() == 2, "coordinateTypes array=2");

            try {
                l.findVarHandle(Box.class, "nope", int.class);
                check(false, "findVarHandle missing must throw");
            } catch (NoSuchFieldException expected) {
            }
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("lookupAndFind: " + e);
        }
    }

    // ---- 2. accessor ops（invoke-polymorphic → guard_* 形）----

    static void accessorOps() {
        try {
            MethodHandles.Lookup l = MethodHandles.lookup();
            VarHandle vi = l.findVarHandle(Box.class, "i", int.class);
            VarHandle vj = l.findVarHandle(Box.class, "j", long.class);
            VarHandle vo = l.findVarHandle(Box.class, "o", Object.class);
            VarHandle vz = l.findVarHandle(Box.class, "z", boolean.class);
            VarHandle vd = l.findVarHandle(Box.class, "d", double.class);
            VarHandle vs = l.findStaticVarHandle(MhChecks.class, "sInt", int.class);
            VarHandle va = MethodHandles.arrayElementVarHandle(int[].class);

            Box b = new Box();
            check((int) vi.get(b) == 0, "get int default");
            vi.set(b, 5);
            check(b.i == 5, "set int");
            check((int) vi.getVolatile(b) == 5, "getVolatile int");
            vi.setVolatile(b, 6);
            check((int) vi.getOpaque(b) == 6, "getOpaque int");
            vi.setOpaque(b, 7);
            check((int) vi.getAcquire(b) == 7, "getAcquire int");
            vi.setRelease(b, 8);
            check(b.i == 8, "setRelease int");

            check(vi.compareAndSet(b, 8, 9), "compareAndSet true");
            check(b.i == 9, "compareAndSet wrote");
            check(!vi.compareAndSet(b, 8, 10), "compareAndSet false");
            check(vi.weakCompareAndSetPlain(b, 9, 11), "weakCompareAndSetPlain");
            check(vi.weakCompareAndSet(b, 11, 12), "weakCompareAndSet");
            check(vi.weakCompareAndSetAcquire(b, 12, 13), "weakCompareAndSetAcquire");
            check(vi.weakCompareAndSetRelease(b, 13, 14), "weakCompareAndSetRelease");

            check((int) vi.getAndSet(b, 20) == 14, "getAndSet int old");
            check(b.i == 20, "getAndSet int wrote");
            check((int) vi.getAndAdd(b, 3) == 20, "getAndAdd int old");
            check(b.i == 23, "getAndAdd int wrote");
            check((int) vi.getAndBitwiseOr(b, 0x10) == 23, "getAndBitwiseOr old");
            check(b.i == (23 | 0x10), "getAndBitwiseOr wrote");
            check((int) vi.getAndBitwiseAnd(b, 0x1f) == (23 | 0x10), "getAndBitwiseAnd old");
            check((int) vi.getAndBitwiseXor(b, 0x01) == ((23 | 0x10) & 0x1f), "getAndBitwiseXor old");
            int afterXor = b.i;

            check((int) vi.compareAndExchange(b, afterXor, 55) == afterXor,
                    "compareAndExchange witness");
            check(b.i == 55, "compareAndExchange wrote");
            check((int) vi.compareAndExchangeAcquire(b, 55, 56) == 55, "compareAndExchangeAcquire");
            check((int) vi.compareAndExchangeRelease(b, 56, 57) == 56, "compareAndExchangeRelease");

            // long
            vj.set(b, 100L);
            check((long) vj.get(b) == 100L, "get long");
            check((long) vj.getAndAdd(b, 5L) == 100L, "getAndAdd long");
            check((long) vj.get(b) == 105L, "getAndAdd long wrote");

            // Object（引用 CAS 语义：== 比较）
            Object o1 = new Object();
            Object o2 = new Object();
            vo.set(b, o1);
            check(vo.get(b) == o1, "get Object");
            check(!vo.compareAndSet(b, o2, o2), "compareAndSet ref identity false");
            check(vo.compareAndSet(b, o1, o2), "compareAndSet ref identity true");
            check(vo.get(b) == o2, "compareAndSet ref wrote");

            // boolean（Z 形状 + coerce）
            vz.set(b, true);
            check((boolean) vz.get(b), "get boolean");
            check(vz.compareAndSet(b, true, false), "compareAndSet boolean");
            check(!((boolean) vz.get(b)), "compareAndSet boolean wrote");

            // double
            vd.set(b, 1.5d);
            check((double) vd.get(b) == 1.5d, "get double");
            check((double) vd.getAndAdd(b, 0.25d) == 1.5d, "getAndAdd double");

            // static field
            check((int) vs.get() == 7, "static get");
            vs.set(70);
            check(sInt == 70, "static set");
            check((int) vs.getAndSet(71) == 70, "static getAndSet");
            check(vs.compareAndSet(71, 72), "static compareAndSet");

            // array element
            int[] xs = {1, 2, 3};
            check((int) va.get(xs, 1) == 2, "array get");
            va.set(xs, 1, 20);
            check(xs[1] == 20, "array set");
            check((int) va.getAndAdd(xs, 1, 5) == 20, "array getAndAdd");
            check(xs[1] == 25, "array getAndAdd wrote");
            check(va.compareAndSet(xs, 1, 25, 26), "array compareAndSet");

            Object[] os = new Object[2];
            VarHandle vao = MethodHandles.arrayElementVarHandle(Object[].class);
            vao.set(os, 0, o1);
            check(vao.get(os, 0) == o1, "array Object get");

            long[] ls = new long[1];
            VarHandle val = MethodHandles.arrayElementVarHandle(long[].class);
            check((long) val.getAndSet(ls, 0, 9L) == 0L, "array long getAndSet");
            check(ls[0] == 9L, "array long wrote");
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("accessorOps: " + e);
        }
    }

    // ---- 3. guard_* 直调（d8 脱糖形状 + mode 区分）----

    static void guardShapes() {
        try {
            MethodHandles.Lookup l = MethodHandles.lookup();
            VarHandle vi = l.findVarHandle(Box.class, "i", int.class);
            VarHandle vs = l.findStaticVarHandle(MhChecks.class, "sInt", int.class);
            VarHandle va = MethodHandles.arrayElementVarHandle(int[].class);

            Box b = new Box();
            VarHandle.AccessDescriptor getAd = VarHandle.AccessDescriptor.of("get", int.class);
            VarHandle.AccessDescriptor setAd = VarHandle.AccessDescriptor.of("set", int.class);
            VarHandle.AccessDescriptor casAd = VarHandle.AccessDescriptor.of("compareAndSet", boolean.class);
            VarHandle.AccessDescriptor addAd = VarHandle.AccessDescriptor.of("getAndAdd", int.class);

            // instance get int：guard_L_I
            vi.set(b, 3);
            check(VarHandleGuards.guard_L_I(vi, b, getAd) == 3, "guard_L_I get");
            // instance set int：guard_LI_V
            VarHandleGuards.guard_LI_V(vi, b, 4, setAd);
            check(b.i == 4, "guard_LI_V set");
            // instance CAS int：guard_LII_Z
            check(VarHandleGuards.guard_LII_Z(vi, b, 4, 5, casAd), "guard_LII_Z cas true");
            check(!VarHandleGuards.guard_LII_Z(vi, b, 4, 6, casAd), "guard_LII_Z cas false");
            // instance getAndAdd int：guard_LI_I（与 array get 同形状——mode 区分语义）
            check(VarHandleGuards.guard_LI_I(vi, b, 2, addAd) == 5, "guard_LI_I getAndAdd");
            check(b.i == 7, "guard_LI_I wrote");

            // static get/set：guard__I / guard_I_V
            sInt = 11;
            check(VarHandleGuards.guard__I(vs, getAd) == 11, "guard__I static get");
            VarHandleGuards.guard_I_V(vs, 12, setAd);
            check(sInt == 12, "guard_I_V static set");

            // array get（同 guard_LI_I 形状，mode=GET）
            int[] xs = {30};
            check(VarHandleGuards.guard_LI_I(va, xs, 0, getAd) == 30, "guard_LI_I array get");

            // 引用形 guard_L_L / guard_LLL_Z
            VarHandle vo = l.findVarHandle(Box.class, "o", Object.class);
            Object o1 = new Object();
            Object o2 = new Object();
            vo.set(b, o1);
            check(VarHandleGuards.guard_L_L(vo, b, VarHandle.AccessDescriptor.of("get", Object.class)) == o1,
                    "guard_L_L ref get");
            VarHandle.AccessDescriptor refCas = VarHandle.AccessDescriptor.of("compareAndSet", boolean.class);
            check(VarHandleGuards.guard_LLL_Z(vo, b, o1, o2, refCas), "guard_LLL_Z ref cas");
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new IllegalStateException("guardShapes: " + e);
        }
    }

    // ---- 4. MethodHandle marker 面 ----

    static void methodHandleMarkers() {
        try {
            MethodHandles.Lookup l = MethodHandles.lookup();
            MethodType twoInts = MethodType.methodType(int.class, int.class, int.class);
            MethodHandle addMh = l.findStatic(MhChecks.class, "add", twoInts);
            check(addMh != null, "findStatic add");
            check(addMh.type().returnType() == int.class, "mh type return");
            check(addMh.type().parameterCount() == 2, "mh type params");

            // invokeBasic（invoke-polymorphic marker → MemberName 目标派发）
            check((int) addMh.invokeBasic(2, 3) == 5, "invokeBasic static");
            // invokeExact / invoke（同基础语义）
            check((int) addMh.invokeExact(20, 30) == 50, "invokeExact static");
            check((int) addMh.invoke(100, 1) == 101, "invoke static");

            // invokeWithArguments（擦除形 → 反射派发）
            check((int) addMh.invokeWithArguments(7, 8) == 15, "invokeWithArguments");

            // linkToStatic 直调（MemberName marker 形）
            {
                java.lang.invoke.MemberName mn = new java.lang.invoke.MemberName(
                        MhChecks.class, "add", "(II)I", java.lang.invoke.MemberName.REF_invokeStatic);
                check((int) MethodHandle.linkToStatic(40, 2, mn) == 42, "linkToStatic");
            }

            // 虚目标：Box.twice
            MethodType oneInt = MethodType.methodType(int.class, int.class);
            MethodHandle twiceMh = l.findVirtual(Box.class, "twice", oneInt);
            Box b = new Box();
            check((int) twiceMh.invokeBasic(b, 5) == 10, "invokeBasic virtual");

            // linkToVirtual
            java.lang.invoke.MemberName vmn = new java.lang.invoke.MemberName(
                    Box.class, "twice", "(I)I", java.lang.invoke.MemberName.REF_invokeVirtual);
            check((int) MethodHandle.linkToVirtual(b, 6, vmn) == 12, "linkToVirtual");

            // MethodHandleInfo 形状（SerializedLambda 消费面）
            MethodHandleInfo info = new MethodHandleInfo(java.lang.invoke.MemberName.REF_invokeStatic,
                    MhChecks.class, "add", twoInts);
            check("invokeStatic".equals(MethodHandleInfo.referenceKindToString(info.getKind())),
                    "MethodHandleInfo kind string");
        } catch (Throwable t) {
            if (t instanceof IllegalStateException) throw (IllegalStateException) t;
            throw new IllegalStateException("methodHandleMarkers: " + t);
        }
    }

    // ---- 5. CLQ 全链 ----

    static void clqChain() {
        ConcurrentLinkedQueue<String> q = new ConcurrentLinkedQueue<>();
        check(q.isEmpty(), "clq empty");
        check(q.offer("a"), "clq offer a");
        check(q.offer("b"), "clq offer b");
        check(q.size() == 2, "clq size");
        check("a".equals(q.poll()), "clq poll a");
        check("b".equals(q.poll()), "clq poll b");
        check(q.poll() == null, "clq poll empty");
    }
}
