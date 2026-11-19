package java.lang.invoke;

/**
 * 裁剪实现：成员标识（方法/字段的 declaring + name + descriptor + refKind）。
 *
 * 原版 MemberName（953 行）深度耦合 MethodHandleNatives 解析引擎（resolve/
 * getMembers native 链），引擎后置；本类只保留 dispatch 消费面：
 *   - MethodHandle.linkTo 系/invokeBasic marker 面（解释器按字段解析目标方法）
 *   - Lookup.findVirtual/findStatic 产物标识
 * VM 侧（core/interpreter.cc invoke-polymorphic）按字段名读取 declaringClass/
 * name/descriptor/refKind——布局契约 = 字段名级（非偏移硬编码）。
 */
public final class MemberName {
    // refKind（java.lang.invoke.MemberName 约定值）
    public static final int REF_getField = 1;
    public static final int REF_getStatic = 2;
    public static final int REF_putField = 3;
    public static final int REF_putStatic = 4;
    public static final int REF_invokeVirtual = 5;
    public static final int REF_invokeStatic = 6;
    public static final int REF_invokeSpecial = 7;
    public static final int REF_newInvokeSpecial = 8;
    public static final int REF_invokeInterface = 9;

    // VM dispatch 读取面（按字段名取；勿改名）
    public final Class<?> declaringClass;
    public final String name;
    public final String descriptor;  // 方法 "(II)I" / 字段 "I" 形态
    public final int refKind;

    public MemberName(Class<?> declaringClass, String name, String descriptor, int refKind) {
        if (declaringClass == null || name == null || descriptor == null) {
            throw new NullPointerException("MemberName null arg");
        }
        this.declaringClass = declaringClass;
        this.name = name;
        this.descriptor = descriptor;
        this.refKind = refKind;
    }

    public boolean isMethod() {
        return refKind >= REF_invokeVirtual && refKind <= REF_invokeInterface;
    }

    public boolean isStatic() {
        return refKind == REF_invokeStatic || refKind == REF_getStatic || refKind == REF_putStatic;
    }

    public Class<?> getDeclaringClass() { return declaringClass; }

    public String getName() { return name; }

    public String getDescriptor() { return descriptor; }

    public int getRefKind() { return refKind; }

    @Override
    public String toString() {
        return "MemberName(" + declaringClass.getName() + "." + name + descriptor + ", kind="
                + refKind + ")";
    }
}
