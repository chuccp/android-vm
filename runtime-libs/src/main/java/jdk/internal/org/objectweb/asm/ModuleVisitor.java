package jdk.internal.org.objectweb.asm;

public class ModuleVisitor {
    protected ModuleVisitor mv;

    public ModuleVisitor(int api) {
    }

    public ModuleVisitor(int api, ModuleVisitor mv) {
        this.mv = mv;
    }

    public void visitMainClass(String mainClass) {
    }

    public void visitPackage(String packaze) {
    }

    public void visitRequire(String module, int access, String version) {
    }

    public void visitExport(String packaze, int access, String... modules) {
    }

    public void visitOpen(String packaze, int access, String... modules) {
    }

    public void visitUse(String service) {
    }

    public void visitProvide(String service, String... providers) {
    }

    public void visitEnd() {
    }
}
