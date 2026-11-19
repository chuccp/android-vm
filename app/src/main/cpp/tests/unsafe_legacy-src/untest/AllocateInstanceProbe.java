package untest;

import sun.misc.Unsafe;

/**
 * allocateInstance（v1 头号刚需）：绕构造器分配。语义对照 ART
 * jni_internal.cc AllocObject：EnsureInitialized 先行（<clinit> 跑），
 * abstract/interface 抛 InstantiationException，实例字段全零。
 */
public class AllocateInstanceProbe {

    static class Ctor {
        int x = 99;
        Object o = new Object();

        Ctor() {
            x = 7;
        }
    }

    static class Throwing {
        int v;

        Throwing() {
            throw new IllegalStateException("ctor must not run");
        }
    }

    abstract static class AbstractType {
        int q;
    }

    static class WithClinit {
        static int marker;

        static {
            marker = 5;
        }

        int field;
    }

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();
        try {
            Unsafe u = Unsafe.getUnsafe();

            Ctor c = (Ctor) u.allocateInstance(Ctor.class);
            sb.append(check(c instanceof Ctor, "allocateInstance-type"));
            sb.append(check(c.x == 0, "allocateInstance-ctor-bypassed"));
            sb.append(check(c.o == null, "allocateInstance-field-init-bypassed"));

            Throwing t = (Throwing) u.allocateInstance(Throwing.class);
            sb.append(check(t.v == 0, "allocateInstance-throwing-ctor"));

            WithClinit w = (WithClinit) u.allocateInstance(WithClinit.class);
            sb.append(check(WithClinit.marker == 5, "allocateInstance-clinit-runs"));
            sb.append(check(w.field == 0, "allocateInstance-zeroed"));
            w.field = 3;
            sb.append(check(w.field == 3, "allocateInstance-usable"));

            try {
                u.allocateInstance(AbstractType.class);
                sb.append("allocateInstance-abstract: FAIL no throw\n");
            } catch (InstantiationException expected) {
                sb.append("allocateInstance-abstract: OK\n");
            }
        } catch (Throwable t2) {
            sb.append("allocate-thrown: FAIL " + t2 + "\n");
        }
        return sb.toString();
    }
}
