package java.lang;

import java.lang.annotation.Annotation;
import java.lang.module.ModuleDescriptor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.net.URI;
import java.nio.charset.CharacterCodingException;
import java.security.AccessControlContext;
import java.security.ProtectionDomain;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Stream;

import jdk.internal.misc.JavaLangAccess;
import jdk.internal.module.ServicesCatalog;
import sun.nio.ch.Interruptible;
import sun.reflect.annotation.AnnotationType;

/**
 * jdk.internal.misc.JavaLangAccess 实现（原 java.lang.System.initPhase1 内匿名类）。
 *
 * 本 VM 的 java.lang.System 由 VM 内建承载，lib 版 initPhase1 不执行，
 * SharedSecrets.getJavaLangAccess() 恒 null —— JDK 11 的 EnumSet/
 * ServiceLoader/NIO 等借用面会在首次使用时 NPE（fastjson2 2.0.66 的
 * Stream/Collectors 链即命中）。此实现放在 java.lang 包内（可访问
 * package-private 成员），由 SharedSecrets 惰性安装。
 */
public final class JavaLangAccessImpl implements JavaLangAccess {
    public JavaLangAccessImpl() {
    }

    public List<Method> getDeclaredPublicMethods(Class<?> klass, String name, Class<?>... parameterTypes) {
        return klass.getDeclaredPublicMethods(name, parameterTypes);
    }

    public jdk.internal.reflect.ConstantPool getConstantPool(Class<?> klass) {
        return klass.getConstantPool();
    }

    public boolean casAnnotationType(Class<?> klass, AnnotationType oldType, AnnotationType newType) {
        return klass.casAnnotationType(oldType, newType);
    }

    public AnnotationType getAnnotationType(Class<?> klass) {
        return klass.getAnnotationType();
    }

    public Map<Class<? extends Annotation>, Annotation> getDeclaredAnnotationMap(Class<?> klass) {
        return klass.getDeclaredAnnotationMap();
    }

    public byte[] getRawClassAnnotations(Class<?> klass) {
        return klass.getRawAnnotations();
    }

    public byte[] getRawClassTypeAnnotations(Class<?> klass) {
        return klass.getRawTypeAnnotations();
    }

    public byte[] getRawExecutableTypeAnnotations(Executable executable) {
        return Class.getExecutableTypeAnnotationBytes(executable);
    }

    public <E extends Enum<E>> E[] getEnumConstantsShared(Class<E> klass) {
        return klass.getEnumConstantsShared();
    }

    public void blockedOn(Interruptible b) {
        Thread.blockedOn(b);
    }

    public void registerShutdownHook(int slot, boolean registerShutdownInProgress, Runnable hook) {
        Shutdown.add(slot, registerShutdownInProgress, hook);
    }

    public Thread newThreadWithAcc(Runnable target, AccessControlContext acc) {
        return new Thread(target, acc);
    }

    @SuppressWarnings("deprecation")
    public void invokeFinalize(Object o) throws Throwable {
        o.finalize();
    }

    public ConcurrentHashMap<?, ?> createOrGetClassLoaderValueMap(ClassLoader cl) {
        return cl.createOrGetClassLoaderValueMap();
    }

    public Class<?> defineClass(ClassLoader loader, String name, byte[] b, ProtectionDomain pd, String source) {
        return ClassLoader.defineClass1(loader, name, b, 0, b.length, pd, source);
    }

    public Class<?> findBootstrapClassOrNull(ClassLoader cl, String name) {
        return cl.findBootstrapClassOrNull(name);
    }

    public Package definePackage(ClassLoader cl, String name, Module module) {
        return cl.definePackage(name, module);
    }

    public String fastUUID(long lsb, long msb) {
        return Long.fastUUID(lsb, msb);
    }

    public void addNonExportedPackages(ModuleLayer layer) {
        SecurityManager.addNonExportedPackages(layer);
    }

    public void invalidatePackageAccessCache() {
        SecurityManager.invalidatePackageAccessCache();
    }

    public Module defineModule(ClassLoader loader,
                               ModuleDescriptor descriptor,
                               URI uri) {
        return new Module(null, loader, descriptor, uri);
    }

    public Module defineUnnamedModule(ClassLoader loader) {
        return new Module(loader);
    }

    public void addReads(Module m1, Module m2) {
        m1.implAddReads(m2);
    }

    public void addReadsAllUnnamed(Module m) {
        m.implAddReadsAllUnnamed();
    }

    public void addExports(Module m, String pn, Module other) {
        m.implAddExports(pn, other);
    }

    public void addExportsToAllUnnamed(Module m, String pn) {
        m.implAddExportsToAllUnnamed(pn);
    }

    public void addOpens(Module m, String pn, Module other) {
        m.implAddOpens(pn, other);
    }

    public void addOpensToAllUnnamed(Module m, String pn) {
        m.implAddOpensToAllUnnamed(pn);
    }

    public void addOpensToAllUnnamed(Module m, Iterator<String> packages) {
        m.implAddOpensToAllUnnamed(packages);
    }

    public void addUses(Module m, Class<?> service) {
        m.implAddUses(service);
    }

    public boolean isReflectivelyExported(Module m, String pn, Module other) {
        return m.isReflectivelyExported(pn, other);
    }

    public boolean isReflectivelyOpened(Module m, String pn, Module other) {
        return m.isReflectivelyOpened(pn, other);
    }

    public ServicesCatalog getServicesCatalog(ModuleLayer layer) {
        return layer.getServicesCatalog();
    }

    public Stream<ModuleLayer> layers(ModuleLayer layer) {
        return layer.layers();
    }

    public Stream<ModuleLayer> layers(ClassLoader loader) {
        return ModuleLayer.layers(loader);
    }

    public String newStringNoRepl(byte[] bytes, java.nio.charset.Charset cs) throws CharacterCodingException {
        return StringCoding.newStringNoRepl(bytes, cs);
    }

    public byte[] getBytesNoRepl(String s, java.nio.charset.Charset cs) throws CharacterCodingException {
        return StringCoding.getBytesNoRepl(s, cs);
    }

    public String newStringUTF8NoRepl(byte[] bytes, int off, int len) {
        return StringCoding.newStringUTF8NoRepl(bytes, off, len);
    }

    public byte[] getBytesUTF8NoRepl(String s) {
        return StringCoding.getBytesUTF8NoRepl(s);
    }

    public String getLoaderNameID(ClassLoader loader) {
        return loader != null ? loader.nameAndId() : "null";
    }
}
