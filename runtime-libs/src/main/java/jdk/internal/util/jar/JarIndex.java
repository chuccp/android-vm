package jdk.internal.util.jar;

// 生成补源（sun-compat）：jdk.internal.util.jar.JarIndex（JarVerifier/JarInputStream 引用）。
public class JarIndex {
    public static final String INDEX_NAME = "META-INF/INDEX.LIST";
    public JarIndex() {
    }

    public JarIndex(java.util.Map<String, java.util.List<String>> map) {
    }

    public java.util.List<String> get(String name) {
        return null;
    }

    public java.lang.String[] getJarFiles() {
        return new java.lang.String[0];
    }

    public void write(java.io.DataOutputStream out) throws java.io.IOException {
    }

    public static JarIndex read(java.io.InputStream in) throws java.io.IOException {
        return new JarIndex();
    }

    public void add(String name, String jarName) {
    }
}
