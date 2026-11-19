package jdk.internal.misc;

// 生成补源（sun-compat）：OpenJDK 11 JavaLangAccess 全面（签名对齐
// java.lang.System 匿名实现，保证 implements 闭包一致）。
import java.io.IOException;
import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.net.URI;
import java.nio.charset.Charset;
import java.security.ProtectionDomain;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Stream;

public interface JavaLangAccess {
    List<Method> getDeclaredPublicMethods(Class<?> klass, String name, Class<?>... parameterTypes);
    jdk.internal.reflect.ConstantPool getConstantPool(Class<?> klass);
    boolean casAnnotationType(Class<?> klass, sun.reflect.annotation.AnnotationType oldType,
                              sun.reflect.annotation.AnnotationType newType);
    sun.reflect.annotation.AnnotationType getAnnotationType(Class<?> klass);
    Map<Class<? extends java.lang.annotation.Annotation>, java.lang.annotation.Annotation>
            getDeclaredAnnotationMap(Class<?> klass);
    byte[] getRawClassAnnotations(Class<?> klass);
    byte[] getRawClassTypeAnnotations(Class<?> klass);
    byte[] getRawExecutableTypeAnnotations(Executable executable);
    void blockedOn(sun.nio.ch.Interruptible b);
    void registerShutdownHook(int slot, boolean registerShutdownInProgress, Runnable hook);
    Thread newThreadWithAcc(Runnable target, java.security.AccessControlContext acc);
    void invokeFinalize(Object o) throws Throwable;
    ConcurrentHashMap<?, ?> createOrGetClassLoaderValueMap(ClassLoader cl);
    Class<?> defineClass(ClassLoader loader, String name, byte[] b,
                         ProtectionDomain pd, String source);
    Class<?> findBootstrapClassOrNull(ClassLoader cl, String name);
    Package definePackage(ClassLoader cl, String name, java.lang.Module module);
    String fastUUID(long lsb, long msb);
    void addNonExportedPackages(java.lang.ModuleLayer layer);
    void invalidatePackageAccessCache();
    java.lang.Module defineModule(ClassLoader loader, java.lang.module.ModuleDescriptor descriptor, URI uri);
    java.lang.Module defineUnnamedModule(ClassLoader loader);
    void addReads(java.lang.Module m1, java.lang.Module m2);
    void addReadsAllUnnamed(java.lang.Module m);
    void addExports(java.lang.Module m, String pn, java.lang.Module other);
    void addExportsToAllUnnamed(java.lang.Module m, String pn);
    void addOpens(java.lang.Module m, String pn, java.lang.Module other);
    void addOpensToAllUnnamed(java.lang.Module m, String pn);
    void addOpensToAllUnnamed(java.lang.Module m, Iterator<String> packages);
    void addUses(java.lang.Module m, Class<?> service);
    boolean isReflectivelyExported(java.lang.Module m, String pn, java.lang.Module other);
    boolean isReflectivelyOpened(java.lang.Module m, String pn, java.lang.Module other);
    jdk.internal.module.ServicesCatalog getServicesCatalog(java.lang.ModuleLayer layer);
    Stream<java.lang.ModuleLayer> layers(java.lang.ModuleLayer layer);
    Stream<java.lang.ModuleLayer> layers(ClassLoader loader);
    String newStringNoRepl(byte[] bytes, Charset cs) throws java.nio.charset.CharacterCodingException;
    byte[] getBytesNoRepl(String s, Charset cs) throws java.nio.charset.CharacterCodingException;
    String newStringUTF8NoRepl(byte[] bytes, int off, int len);
    byte[] getBytesUTF8NoRepl(String s);
    String getLoaderNameID(ClassLoader loader);
    <E extends Enum<E>> E[] getEnumConstantsShared(Class<E> klass);
}
