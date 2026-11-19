package jdk.internal.org.objectweb.asm;

public class ClassWriter extends ClassVisitor {
    public static final int COMPUTE_MAXS = 1;
    public static final int COMPUTE_FRAMES = 2;

    public ClassWriter(int flags) {
        super(0);
    }

    public ClassWriter(int flags, ClassVisitor classVisitor) {
        super(0, classVisitor);
    }

    public byte[] toByteArray() {
        throw new UnsupportedOperationException("compile stub");
    }

    public Object newConst(Object value) {
        throw new UnsupportedOperationException("compile stub");
    }

    public String newUTF8(String value) {
        throw new UnsupportedOperationException("compile stub");
    }
}
