package java.lang.invoke;

import jdk.internal.misc.SharedSecrets;

/**
 * 裁剪实现：MethodHandleNatives 原版为全 native 引擎接缝（resolve/getMembers/
 * objectFieldOffset/expand …，C 侧 methodtypenatives.c）——引擎后置 + 零 native
 * shim 政策（JLI 引擎是 Java）下不保留 native 面。本类仅保留：
 *   1. Constants 标志位（MemberName refKind 语义锚）；
 *   2. JavaLangInvokeAccess 注册（StackFrameInfo.getMethodType 消费面）。
 * linkTo 系/invokeBasic marker 形声明在 MethodHandle（dex 引用形态一致）。
 */
final class MethodHandleNatives {

    private MethodHandleNatives() { }

    static final class Constants {
        // MemberName refKind（与 java.lang.invoke.MemberName 约定一致）
        public static final int REF_NONE = 0;
        public static final int REF_getField = 1;
        public static final int REF_getStatic = 2;
        public static final int REF_putField = 3;
        public static final int REF_putStatic = 4;
        public static final int REF_invokeVirtual = 5;
        public static final int REF_invokeStatic = 6;
        public static final int REF_invokeSpecial = 7;
        public static final int REF_newInvokeSpecial = 8;
        public static final int REF_invokeInterface = 9;

        public static final int MN_IS_METHOD = 0x00010000;
        public static final int MN_IS_CONSTRUCTOR = 0x00020000;
        public static final int MN_IS_FIELD = 0x00040000;
        public static final int MN_IS_TYPE = 0x00080000;

        private Constants() { }
    }

    static {
        SharedSecrets.setJavaLangInvokeAccess(new JavaLangInvokeAccessImpl());
    }

    /** 保证 <clinit> 可触发（注册 JavaLangInvokeAccess）。 */
    static void ensureInitialized() { }

    private static final class JavaLangInvokeAccessImpl
            implements jdk.internal.misc.JavaLangInvokeAccess {
        @Override
        public Object newMemberName() {
            return new MemberName(Object.class, "", "()V", Constants.REF_NONE);
        }

        @Override
        public Class<?> getDeclaringClass(Object memberName) {
            return ((MemberName) memberName).declaringClass;
        }

        @Override
        public String getName(Object memberName) {
            return ((MemberName) memberName).name;
        }

        @Override
        public java.lang.invoke.MethodType getMethodType(Object memberName) {
            MemberName mn = (MemberName) memberName;
            return MethodType.fromDescriptor(mn.descriptor, mn.declaringClass.getClassLoader());
        }

        @Override
        public String getMethodDescriptor(Object memberName) {
            return ((MemberName) memberName).descriptor;
        }

        @Override
        public boolean isNative(Object memberName) {
            return false;
        }

        @Override
        public String toString(Object memberName) {
            return String.valueOf(memberName);
        }
    }
}
