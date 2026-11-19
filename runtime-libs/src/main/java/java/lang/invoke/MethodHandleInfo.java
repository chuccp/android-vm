package java.lang.invoke;

import java.util.Objects;

/**
 * 补源（java.lang.invoke 家族修复）：最小 MethodHandleInfo。
 * 原版依赖 MethodHandleNatives.Constants/MethodHandleStatics（引擎链，已后置）；
 * 本类为纯标识视图（成员种类/名称/类型），供 SerializedLambda.readResolve
 * 与后续 lambda metafactory 面复用。
 */
public final class MethodHandleInfo {

    public static final int REF_getField = MemberName.REF_getField;
    public static final int REF_getStatic = MemberName.REF_getStatic;
    public static final int REF_putField = MemberName.REF_putField;
    public static final int REF_putStatic = MemberName.REF_putStatic;
    public static final int REF_invokeVirtual = MemberName.REF_invokeVirtual;
    public static final int REF_invokeStatic = MemberName.REF_invokeStatic;
    public static final int REF_invokeSpecial = MemberName.REF_invokeSpecial;
    public static final int REF_newInvokeSpecial = MemberName.REF_newInvokeSpecial;
    public static final int REF_invokeInterface = MemberName.REF_invokeInterface;

    private final Class<?> declaringClass;
    private final String name;
    private final MethodType methodType;
    private final int referenceKind;

    public MethodHandleInfo(int referenceKind, Class<?> declaringClass, String name,
            MethodType methodType) {
        this.referenceKind = referenceKind;
        this.declaringClass = Objects.requireNonNull(declaringClass, "null declaringClass");
        this.name = Objects.requireNonNull(name, "null name");
        this.methodType = Objects.requireNonNull(methodType, "null methodType");
    }

    public int getKind() { return referenceKind; }

    public Class<?> getDeclaringClass() { return declaringClass; }

    public String getName() { return name; }

    public MethodType getMethodType() { return methodType; }

    public static String referenceKindToString(int referenceKind) {
        switch (referenceKind) {
            case REF_getField: return "getField";
            case REF_getStatic: return "getStatic";
            case REF_putField: return "putField";
            case REF_putStatic: return "putStatic";
            case REF_invokeVirtual: return "invokeVirtual";
            case REF_invokeStatic: return "invokeStatic";
            case REF_invokeSpecial: return "invokeSpecial";
            case REF_newInvokeSpecial: return "newInvokeSpecial";
            case REF_invokeInterface: return "invokeInterface";
            default: return "REF_" + referenceKind;
        }
    }

    @Override
    public String toString() {
        return referenceKindToString(referenceKind) + " " + declaringClass.getName() + "."
                + name + methodType;
    }
}
