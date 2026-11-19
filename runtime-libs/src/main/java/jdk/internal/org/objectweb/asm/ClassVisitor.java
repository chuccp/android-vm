package jdk.internal.org.objectweb.asm;

public class ClassVisitor {
    protected ClassVisitor cv;

    public ClassVisitor(int api) {
    }

    public ClassVisitor(int api, ClassVisitor cv) {
        this.cv = cv;
    }

    public void visit(int version, int access, String name, String signature,
                      String superName, String[] interfaces) {
    }

    public void visitSource(String source, String debug) {
    }

    public void visitOuterClass(String owner, String name, String descriptor) {
    }

    public void visitInnerClass(String name, String outerName,
                                String innerName, int access) {
    }

    public AnnotationVisitor visitAnnotation(String descriptor, boolean visible) {
        return null;
    }

    public void visitAttribute(Attribute attribute) {
    }

    public ModuleVisitor visitModule(String name, int access, String version) {
        return null;
    }

    public void visitNestHost(String nestHost) {
    }

    public void visitNestMember(String nestMember) {
    }

    public FieldVisitor visitField(int access, String name, String descriptor,
                                   String signature, Object value) {
        return null;
    }

    public MethodVisitor visitMethod(int access, String name, String descriptor,
                                     String signature, String[] exceptions) {
        return null;
    }

    public void visitEnd() {
    }
}
