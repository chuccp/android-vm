package jdk.internal.loader;

// 生成补源（sun-compat）：jdk.internal.loader.URLClassPath（URLClassLoader
// 类路径面）。JBR 25 无源；OpenJDK 11 语义子集（URL 表 + 顺序查找）。
public class URLClassPath {
    private final java.util.List<java.net.URL> urls = new java.util.ArrayList<>();

    public URLClassPath(java.net.URL[] urls,
                        java.security.AccessControlContext acc) {
        for (java.net.URL url : urls) {
            this.urls.add(url);
        }
    }

    public URLClassPath(java.net.URL[] urls,
                        java.net.URLStreamHandlerFactory factory,
                        java.security.AccessControlContext acc) {
        for (java.net.URL url : urls) {
            this.urls.add(url);
        }
    }

    public static java.net.URL checkURL(java.net.URL url) {
        return url;
    }

    public void addURL(java.net.URL url) {
        urls.add(url);
    }

    public java.net.URL[] getURLs() {
        return urls.toArray(new java.net.URL[0]);
    }

    public java.util.List<java.io.IOException> closeLoaders() {
        return new java.util.ArrayList<>();
    }

    public Resource getResource(String name, boolean check) {
        for (java.net.URL base : urls) {
            try {
                java.net.URL url = new java.net.URL(base, name);
                java.net.URLConnection conn = url.openConnection();
                conn.setUseCaches(false);
                conn.getInputStream().close();
                return new Resource(name, url);
            } catch (Exception ignored) {
                // 尝试下一个
            }
        }
        return null;
    }

    public java.net.URL findResource(String name, boolean check) {
        Resource res = getResource(name, check);
        return res == null ? null : res.getURL();
    }

    public java.util.Enumeration<java.net.URL> findResources(String name, boolean check) {
        java.util.List<java.net.URL> hits = new java.util.ArrayList<>();
        for (java.net.URL base : urls) {
            try {
                java.net.URL url = new java.net.URL(base, name);
                java.net.URLConnection conn = url.openConnection();
                conn.setUseCaches(false);
                conn.getInputStream().close();
                hits.add(url);
            } catch (Exception ignored) {
                // 尝试下一个
            }
        }
        return java.util.Collections.enumeration(hits);
    }

    public URLClassPath.URLLoader getLoader(int index) {
        return null;
    }

    public interface URLLoader {
        Resource getResource(String name, boolean check);
        java.util.Enumeration<Resource> getResources(String name, boolean check);
    }
}
