package jdk.internal.org.objectweb.asm;

public class MethodVisitor {
    protected MethodVisitor mv;

    public MethodVisitor(int api) {
    }

    public MethodVisitor(int api, MethodVisitor mv) {
        this.mv = mv;
    }

    public void visitParameter(String name, int access) {
    }

    public AnnotationVisitor visitAnnotationDefault() {
        return null;
    }

    public AnnotationVisitor visitAnnotation(String descriptor, boolean visible) {
        return null;
    }

    public void visitAttribute(Attribute attribute) {
    }

    public void visitCode() {
    }

    public void visitFrame(int type, int numLocal, Object[] local,
                           int numStack, Object[] stack) {
    }

    public void visitInsn(int opcode) {
    }

    public void visitIntInsn(int opcode, int operand) {
    }

    public void visitVarInsn(int opcode, int var) {
    }

    public void visitTypeInsn(int opcode, String type) {
    }

    public void visitFieldInsn(int opcode, String owner, String name,
                               String descriptor) {
    }

    public void visitMethodInsn(int opcode, String owner, String name,
                                String descriptor, boolean isInterface) {
    }

    public void visitJumpInsn(int opcode, Label label) {
    }

    public void visitLabel(Label label) {
    }

    public void visitLdcInsn(Object value) {
    }

    public void visitIincInsn(int var, int increment) {
    }

    public void visitTableSwitchInsn(int min, int max, Label dflt, Label... labels) {
    }

    public void visitLookupSwitchInsn(Label dflt, int[] keys, Label[] labels) {
    }

    public void visitMultiANewArrayInsn(String descriptor, int numDimensions) {
    }

    public void visitTryCatchBlock(Label start, Label end, Label handler, String type) {
    }

    public void visitLocalVariable(String name, String descriptor, String signature,
                                   Label start, Label end, int index) {
    }

    public void visitLineNumber(int line, Label start) {
    }

    public void visitMaxs(int maxStack, int maxLocals) {
    }

    public void visitEnd() {
    }
}
