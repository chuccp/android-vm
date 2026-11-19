package jdk.internal.org.objectweb.asm;

public class AnnotationVisitor {
    protected AnnotationVisitor av;

    public AnnotationVisitor(int api) {
    }

    public AnnotationVisitor(int api, AnnotationVisitor av) {
        this.av = av;
    }

    public void visit(String name, Object value) {
    }

    public void visitEnum(String name, String descriptor, String value) {
    }

    public AnnotationVisitor visitAnnotation(String name, String descriptor) {
        return null;
    }

    public AnnotationVisitor visitArray(String name) {
        return null;
    }

    public void visitEnd() {
    }
}
