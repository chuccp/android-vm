package jdk.internal.org.objectweb.asm;

public class FieldVisitor {
    protected FieldVisitor fv;

    public FieldVisitor(int api) {
    }

    public FieldVisitor(int api, FieldVisitor fv) {
        this.fv = fv;
    }

    public AnnotationVisitor visitAnnotation(String descriptor, boolean visible) {
        return null;
    }

    public void visitAttribute(Attribute attribute) {
    }

    public void visitEnd() {
    }
}
