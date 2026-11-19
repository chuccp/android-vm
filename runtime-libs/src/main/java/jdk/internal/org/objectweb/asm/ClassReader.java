package jdk.internal.org.objectweb.asm;

public class ClassReader {
    public static final int SKIP_CODE = 1;
    public static final int SKIP_DEBUG = 2;
    public static final int SKIP_FRAMES = 4;
    public static final int EXPAND_FRAMES = 8;

    public ClassReader(byte[] classFile) {
    }

    public ClassReader(java.io.InputStream in) throws java.io.IOException {
    }

    public void accept(ClassVisitor classVisitor, int parsingOptions) {
    }

    public String getClassName() {
        return "";
    }

    public int getAccess() {
        return 0;
    }
}
