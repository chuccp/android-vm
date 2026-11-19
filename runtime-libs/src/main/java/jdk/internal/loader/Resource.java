package jdk.internal.loader;

// 生成补源（sun-compat）：jdk.internal.loader.Resource（URLClassLoader 资源面）。
public class Resource {
    private final String name;
    private final java.net.URL url;

    public Resource(String name, java.net.URL url) {
        this.name = name;
        this.url = url;
    }

    public String getName() {
        return name;
    }

    public java.net.URL getURL() {
        return url;
    }

    public java.net.URL getCodeSourceURL() {
        return url;
    }

    public java.io.InputStream getInputStream() throws java.io.IOException {
        return url.openStream();
    }

    public byte[] getBytes() throws java.io.IOException {
        try (java.io.InputStream in = getInputStream()) {
            return in.readAllBytes();
        }
    }

    public java.security.CodeSource getCodeSource(java.net.URL url) {
        return null;
    }

    public Throwable getDataError() {
        return null;
    }

    public java.util.jar.Manifest getManifest() throws java.io.IOException {
        return null;
    }

    public java.security.CodeSigner[] getCodeSigners() {
        return null;
    }

    public java.security.cert.Certificate[] getCertificates() {
        return null;
    }

    public java.nio.ByteBuffer getByteBuffer() throws java.io.IOException {
        byte[] bytes = getBytes();
        return java.nio.ByteBuffer.wrap(bytes);
    }

    public java.util.zip.ZipEntry getZipEntry() {
        return null;
    }

}
