package jdk.internal.misc;

// 生成补源（sun-compat，随 runtime.dex 入包）：JBR 25 删除了 jdk.internal.misc.
// SharedSecrets（JEP 471 系列）；vendored OpenJDK 11 源（JarFile/ObjectInputStream/
// Buffer/Console/ResourceBundle 等）经 SharedSecrets 交换访问面。OpenJDK 11 语义：
// 各访问面单例槽 + get/set。
public class SharedSecrets {
    private static JavaObjectInputStreamAccess javaObjectInputStreamAccess;
    private static JavaLangAccess javaLangAccess;
    private static JavaIOAccess javaIOAccess;
    private static JavaIOFileDescriptorAccess javaIOFileDescriptorAccess;
    private static JavaLangRefAccess javaLangRefAccess;
    private static JavaLangInvokeAccess javaLangInvokeAccess;
    private static JavaNioAccess javaNioAccess;
    private static JavaSecurityAccess javaSecurityAccess;
    private static JavaUtilResourceBundleAccess javaUtilResourceBundleAccess;
    private static JavaUtilJarAccess javaUtilJarAccess;
    private static JavaUtilZipFileAccess javaUtilZipFileAccess =
        new DefaultZipFileAccess();

    public static void setJavaObjectInputStreamAccess(JavaObjectInputStreamAccess access) {
        javaObjectInputStreamAccess = access;
    }

    public static JavaObjectInputStreamAccess getJavaObjectInputStreamAccess() {
        return javaObjectInputStreamAccess;
    }


    public static void setJavaLangAccess(JavaLangAccess access) {
        javaLangAccess = access;
    }

    public static JavaLangAccess getJavaLangAccess() {
        JavaLangAccess access = javaLangAccess;
        if (access == null) {
            // 本 VM 的 java.lang.System 由内建承载，lib 版 initPhase1 不执行
            //（原注册点 System.setJavaLangAccess），JDK 11 借用面首次使用即
            // NPE。此处惰性安装等价实现（java.lang.JavaLangAccessImpl）。
            access = new java.lang.JavaLangAccessImpl();
            javaLangAccess = access;
        }
        return access;
    }

    public static void setJavaIOAccess(JavaIOAccess access) {
        javaIOAccess = access;
    }

    public static JavaIOAccess getJavaIOAccess() {
        return javaIOAccess;
    }

    public static void setJavaIOFileDescriptorAccess(JavaIOFileDescriptorAccess access) {
        javaIOFileDescriptorAccess = access;
    }

    public static JavaIOFileDescriptorAccess getJavaIOFileDescriptorAccess() {
        if (javaIOFileDescriptorAccess == null) {
            // 注册在 java.io.FileDescriptor <clinit>；静态 fdAccess 读取方
            // （FileOutputStream 等）可能先于该类初始化——惰性触发注册，保证
            // 读取面恒非空（类初始化序不受消费方 <clinit> 文本序约束）。
            java.io.FileDescriptor.ensureAccessRegistered();
        }
        return javaIOFileDescriptorAccess;
    }

    public static void setJavaLangRefAccess(JavaLangRefAccess access) {
        javaLangRefAccess = access;
    }

    public static JavaLangRefAccess getJavaLangRefAccess() {
        return javaLangRefAccess;
    }

    public static void setJavaLangInvokeAccess(JavaLangInvokeAccess access) {
        javaLangInvokeAccess = access;
    }

    public static JavaLangInvokeAccess getJavaLangInvokeAccess() {
        return javaLangInvokeAccess;
    }

    public static void setJavaNioAccess(JavaNioAccess access) {
        javaNioAccess = access;
    }

    public static JavaNioAccess getJavaNioAccess() {
        return javaNioAccess;
    }

    public static void setJavaSecurityAccess(JavaSecurityAccess access) {
        javaSecurityAccess = access;
    }

    public static JavaSecurityAccess getJavaSecurityAccess() {
        return javaSecurityAccess;
    }

    public static void setJavaUtilResourceBundleAccess(JavaUtilResourceBundleAccess access) {
        javaUtilResourceBundleAccess = access;
    }

    public static JavaUtilResourceBundleAccess getJavaUtilResourceBundleAccess() {
        return javaUtilResourceBundleAccess;
    }

    public static void setJavaUtilJarAccess(JavaUtilJarAccess access) {
        javaUtilJarAccess = access;
    }

    public static JavaUtilJarAccess getJavaUtilJarAccess() {
        return javaUtilJarAccess;
    }

    public static void setJavaUtilZipFileAccess(JavaUtilZipFileAccess access) {
        javaUtilZipFileAccess = access;
    }

    public static JavaUtilZipFileAccess getJavaUtilZipFileAccess() {
        return javaUtilZipFileAccess;
    }

    public static JavaUtilJarAccess javaUtilJarAccess() {
        return javaUtilJarAccess;
    }

    public static JavaUtilZipFileAccess javaUtilZipFileAccess() {
        return javaUtilZipFileAccess;
    }

    public static JavaLangAccess javaLangAccess() {
        return javaLangAccess;
    }

    public static JavaIOFileDescriptorAccess javaIOFileDescriptorAccess() {
        return javaIOFileDescriptorAccess;
    }

    private static JavaNetURLAccess javaNetURLAccess;
    private static JavaNetURLClassLoaderAccess javaNetURLClassLoaderAccess;
    private static JavaNetHttpCookieAccess javaNetHttpCookieAccess;
    private static JavaNetSocketAccess javaNetSocketAccess;
    private static JavaNetUriAccess javaNetUriAccess;
    private static JavaNetInetAddressAccess javaNetInetAddressAccess;
    private static JavaSecuritySignatureAccess javaSecuritySignatureAccess;
    private static JavaIOFilePermissionAccess javaIOFilePermissionAccess;
    private static JavaLangModuleAccess javaLangModuleAccess;
    private static JavaSecurityPropertiesAccess javaSecurityPropertiesAccess;
    public interface JavaxCryptoSealedObjectAccess {
        java.io.ObjectInputStream getExtObjectInputStream(
                javax.crypto.SealedObject obj, javax.crypto.Cipher c)
                throws java.io.IOException, javax.crypto.BadPaddingException,
                       javax.crypto.IllegalBlockSizeException;
    }

    public interface JavaObjectInputFilterAccess {
        java.io.ObjectInputFilter createFilter2(String pattern);
    }

    private static JavaObjectInputFilterAccess javaObjectInputFilterAccess;
    private static JavaxCryptoSealedObjectAccess javaxCryptoSealedObjectAccess;

    public static void setJavaSecurityPropertiesAccess(JavaSecurityPropertiesAccess access) {
        javaSecurityPropertiesAccess = access;
    }

    public static JavaSecurityPropertiesAccess getJavaSecurityPropertiesAccess() {
        return javaSecurityPropertiesAccess;
    }

    public static void setJavaObjectInputFilterAccess(JavaObjectInputFilterAccess access) {
        javaObjectInputFilterAccess = access;
    }

    public static void setJavaxCryptoSealedObjectAccess(JavaxCryptoSealedObjectAccess access) {
        javaxCryptoSealedObjectAccess = access;
    }

    public static void setJavaNetURLAccess(JavaNetURLAccess access) {
        javaNetURLAccess = access;
    }

    public static JavaNetURLAccess getJavaNetURLAccess() {
        return javaNetURLAccess;
    }

    public static void setJavaNetURLClassLoaderAccess(JavaNetURLClassLoaderAccess access) {
        javaNetURLClassLoaderAccess = access;
    }

    public static JavaNetURLClassLoaderAccess getJavaNetURLClassLoaderAccess() {
        return javaNetURLClassLoaderAccess;
    }

    public static void setJavaNetHttpCookieAccess(JavaNetHttpCookieAccess access) {
        javaNetHttpCookieAccess = access;
    }

    public static JavaNetHttpCookieAccess getJavaNetHttpCookieAccess() {
        return javaNetHttpCookieAccess;
    }

    public static void setJavaNetSocketAccess(JavaNetSocketAccess access) {
        javaNetSocketAccess = access;
    }

    public static JavaNetSocketAccess getJavaNetSocketAccess() {
        return javaNetSocketAccess;
    }

    public static void setJavaNetUriAccess(JavaNetUriAccess access) {
        javaNetUriAccess = access;
    }

    public static JavaNetUriAccess getJavaNetUriAccess() {
        return javaNetUriAccess;
    }

    public static void setJavaNetInetAddressAccess(JavaNetInetAddressAccess access) {
        javaNetInetAddressAccess = access;
    }

    public static JavaNetInetAddressAccess getJavaNetInetAddressAccess() {
        return javaNetInetAddressAccess;
    }

    public static void setJavaSecuritySignatureAccess(JavaSecuritySignatureAccess access) {
        javaSecuritySignatureAccess = access;
    }

    public static JavaSecuritySignatureAccess getJavaSecuritySignatureAccess() {
        return javaSecuritySignatureAccess;
    }

    public static void setJavaIOFilePermissionAccess(JavaIOFilePermissionAccess access) {
        javaIOFilePermissionAccess = access;
    }

    public static JavaIOFilePermissionAccess getJavaIOFilePermissionAccess() {
        return javaIOFilePermissionAccess;
    }

    public static void setJavaLangModuleAccess(JavaLangModuleAccess access) {
        javaLangModuleAccess = access;
    }

    public static JavaLangModuleAccess getJavaLangModuleAccess() {
        return javaLangModuleAccess;
    }

    public interface JavaObjectInputStreamReadStringAccess {
        String readString(java.io.ObjectInputStream in) throws java.io.IOException;
    }

    private static JavaObjectInputStreamReadStringAccess javaObjectInputStreamReadStringAccess;

    public static void setJavaObjectInputStreamReadString(
            JavaObjectInputStreamReadStringAccess access) {
        javaObjectInputStreamReadStringAccess = access;
    }

    public static JavaObjectInputStreamReadStringAccess getJavaObjectInputStreamReadString() {
        return javaObjectInputStreamReadStringAccess;
    }

    // 默认 zip 面（JarFile.<clinit> 之外的使用路径兜底；语义走 java.util.zip 公有面）
    private static final class DefaultZipFileAccess implements JavaUtilZipFileAccess {
        @Override
        public int getManifestNum(java.util.jar.JarFile jar) {
            return 1;
        }

        @Override
        public String[] getMetaInfEntryNames(java.util.zip.ZipFile zip) {
            java.util.List<String> names = new java.util.ArrayList<>();
            java.util.Enumeration<? extends java.util.zip.ZipEntry> en = zip.entries();
            while (en.hasMoreElements()) {
                String name = en.nextElement().getName();
                if (name.startsWith("META-INF/")) {
                    names.add(name);
                }
            }
            return names.toArray(new String[0]);
        }

        @Override
        public <T extends java.util.zip.ZipEntry> java.util.Enumeration<T> entries(
                java.util.jar.JarFile jar,
                java.util.function.Function<String, T> fn) {
            java.util.List<T> list = new java.util.ArrayList<>();
            java.util.Enumeration<java.util.jar.JarEntry> en = jar.entries();
            while (en.hasMoreElements()) {
                list.add(fn.apply(en.nextElement().getName()));
            }
            return java.util.Collections.enumeration(list);
        }

        @Override
        public <T extends java.util.zip.ZipEntry> java.util.stream.Stream<T> stream(
                java.util.jar.JarFile jar,
                java.util.function.Function<String, T> fn) {
            java.util.List<T> list = new java.util.ArrayList<>();
            java.util.Enumeration<java.util.jar.JarEntry> en = jar.entries();
            while (en.hasMoreElements()) {
                list.add(fn.apply(en.nextElement().getName()));
            }
            return list.stream();
        }

        @Override
        public java.util.stream.Stream<String> entryNameStream(java.util.jar.JarFile jar) {
            return jar.stream().map(java.util.zip.ZipEntry::getName);
        }

        @Override
        public java.util.zip.ZipEntry getEntry(
                java.util.zip.ZipFile zip, String name,
                java.util.function.Function<String, ? extends java.util.zip.ZipEntry> fn) {
            java.util.zip.ZipEntry entry = zip.getEntry(name);
            return entry == null ? null : fn.apply(name);
        }
    }
}
