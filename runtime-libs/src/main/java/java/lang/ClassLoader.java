package java.lang;

import java.io.InputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.io.File;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.net.URL;
import java.security.AccessController;
import java.security.AccessControlContext;
import java.security.CodeSource;
import java.security.PrivilegedAction;
import java.security.ProtectionDomain;
import java.security.cert.Certificate;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Collections;
import java.util.Deque;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Hashtable;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Objects;
import java.util.Set;
import java.util.Spliterator;
import java.util.Spliterators;
import java.util.Vector;
import java.util.WeakHashMap;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Supplier;
import java.util.stream.Stream;
import java.util.stream.StreamSupport;

import jdk.internal.loader.BuiltinClassLoader;
import jdk.internal.perf.PerfCounter;
import jdk.internal.loader.BootLoader;
import jdk.internal.loader.ClassLoaders;
import jdk.internal.misc.Unsafe;
import jdk.internal.misc.VM;
import jdk.internal.ref.CleanerFactory;
import jdk.internal.reflect.CallerSensitive;
import jdk.internal.reflect.Reflection;
import sun.reflect.misc.ReflectUtil;
import sun.security.util.SecurityConstants;

public abstract class ClassLoader {

    private static native void registerNatives();
    static {
        registerNatives();
    }

    private final ClassLoader parent;

    private final String name;

    private final Module unnamedModule;

    private final String nameAndId;

    private static class ParallelLoaders {
        private ParallelLoaders() {}

        private static final Set<Class<? extends ClassLoader>> loaderTypes =
            Collections.newSetFromMap(new WeakHashMap<>());
        static {
            synchronized (loaderTypes) { loaderTypes.add(ClassLoader.class); }
        }

        static boolean register(Class<? extends ClassLoader> c) {
            synchronized (loaderTypes) {
                if (loaderTypes.contains(c.getSuperclass())) {

                    loaderTypes.add(c);
                    return true;
                } else {
                    return false;
                }
            }
        }

        static boolean isRegistered(Class<? extends ClassLoader> c) {
            synchronized (loaderTypes) {
                return loaderTypes.contains(c);
            }
        }
    }

    private final ConcurrentHashMap<String, Object> parallelLockMap;

    private final Map <String, Certificate[]> package2certs;

    private static final Certificate[] nocerts = new Certificate[0];

    private final Vector<Class<?>> classes = new Vector<>();

    private final ProtectionDomain defaultDomain =
        new ProtectionDomain(new CodeSource(null, (Certificate[]) null),
                             null, this, null);

    void addClass(Class<?> c) {
        classes.addElement(c);
    }

    private final ConcurrentHashMap<String, NamedPackage> packages
            = new ConcurrentHashMap<>();

    private NamedPackage getNamedPackage(String pn, Module m) {
        NamedPackage p = packages.get(pn);
        if (p == null) {
            p = new NamedPackage(pn, m);

            NamedPackage value = packages.putIfAbsent(pn, p);
            if (value != null) {

                p = value;

                assert value.module() == m;
            }
        }
        return p;
    }

    private static Void checkCreateClassLoader() {
        return checkCreateClassLoader(null);
    }

    private static Void checkCreateClassLoader(String name) {
        if (name != null && name.isEmpty()) {
            throw new IllegalArgumentException("name must be non-empty or null");
        }

        SecurityManager security = System.getSecurityManager();
        if (security != null) {
            security.checkCreateClassLoader();
        }
        return null;
    }

    private ClassLoader(Void unused, String name, ClassLoader parent) {
        this.name = name;
        this.parent = parent;
        this.unnamedModule = new Module(this);
        if (ParallelLoaders.isRegistered(this.getClass())) {
            parallelLockMap = new ConcurrentHashMap<>();
            package2certs = new ConcurrentHashMap<>();
            assertionLock = new Object();
        } else {

            parallelLockMap = null;
            package2certs = new Hashtable<>();
            assertionLock = this;
        }
        this.nameAndId = nameAndId(this);
    }

    private static String nameAndId(ClassLoader ld) {
        String nid = ld.getName() != null ? "\'" + ld.getName() + "\'"
                                          : ld.getClass().getName();
        if (!(ld instanceof BuiltinClassLoader)) {
            String id = Integer.toHexString(System.identityHashCode(ld));
            nid = nid + " @" + id;
        }
        return nid;
    }

    String nameAndId() {
        return nameAndId;
    }

    protected ClassLoader(String name, ClassLoader parent) {
        this(checkCreateClassLoader(name), name, parent);
    }

    protected ClassLoader(ClassLoader parent) {
        this(checkCreateClassLoader(), null, parent);
    }

    protected ClassLoader() {
        this(checkCreateClassLoader(), null, getSystemClassLoader());
    }

    public String getName() {
        return name;
    }

    final String name() {
        return name;
    }

    public Class<?> loadClass(String name) throws ClassNotFoundException {
        return loadClass(name, false);
    }

    protected Class<?> loadClass(String name, boolean resolve)
        throws ClassNotFoundException
    {
        synchronized (getClassLoadingLock(name)) {

            Class<?> c = findLoadedClass(name);
            if (c == null) {
                long t0 = System.nanoTime();
                try {
                    if (parent != null) {
                        c = parent.loadClass(name, false);
                    } else {
                        c = findBootstrapClassOrNull(name);
                    }
                } catch (ClassNotFoundException e) {

                }

                if (c == null) {

                    long t1 = System.nanoTime();
                    c = findClass(name);

                    PerfCounter.getParentDelegationTime().addTime(t1 - t0);
                    PerfCounter.getFindClassTime().addElapsedTimeFrom(t1);
                    PerfCounter.getFindClasses().increment();
                }
            }
            if (resolve) {
                resolveClass(c);
            }
            return c;
        }
    }

    final Class<?> loadClass(Module module, String name) {
        synchronized (getClassLoadingLock(name)) {

            Class<?> c = findLoadedClass(name);
            if (c == null) {
                c = findClass(module.getName(), name);
            }
            if (c != null && c.getModule() == module) {
                return c;
            } else {
                return null;
            }
        }
    }

    protected Object getClassLoadingLock(String className) {
        Object lock = this;
        if (parallelLockMap != null) {
            Object newLock = new Object();
            lock = parallelLockMap.putIfAbsent(className, newLock);
            if (lock == null) {
                lock = newLock;
            }
        }
        return lock;
    }

    private void checkPackageAccess(Class<?> cls, ProtectionDomain pd) {
        final SecurityManager sm = System.getSecurityManager();
        if (sm != null) {
            if (ReflectUtil.isNonPublicProxyClass(cls)) {
                for (Class<?> intf: cls.getInterfaces()) {
                    checkPackageAccess(intf, pd);
                }
                return;
            }

            final String packageName = cls.getPackageName();
            if (!packageName.isEmpty()) {
                AccessController.doPrivileged(new PrivilegedAction<>() {
                    public Void run() {
                        sm.checkPackageAccess(packageName);
                        return null;
                    }
                }, new AccessControlContext(new ProtectionDomain[] {pd}));
            }
        }
    }

    protected Class<?> findClass(String name) throws ClassNotFoundException {
        throw new ClassNotFoundException(name);
    }

    protected Class<?> findClass(String moduleName, String name) {
        if (moduleName == null) {
            try {
                return findClass(name);
            } catch (ClassNotFoundException ignore) { }
        }
        return null;
    }

    @Deprecated(since="1.1")
    protected final Class<?> defineClass(byte[] b, int off, int len)
        throws ClassFormatError
    {
        return defineClass(null, b, off, len, null);
    }

    protected final Class<?> defineClass(String name, byte[] b, int off, int len)
        throws ClassFormatError
    {
        return defineClass(name, b, off, len, null);
    }

    private ProtectionDomain preDefineClass(String name,
                                            ProtectionDomain pd)
    {
        if (!checkName(name))
            throw new NoClassDefFoundError("IllegalName: " + name);

        if ((name != null) && name.startsWith("java.")
                && this != getBuiltinPlatformClassLoader()) {
            throw new SecurityException
                ("Prohibited package name: " +
                 name.substring(0, name.lastIndexOf('.')));
        }
        if (pd == null) {
            pd = defaultDomain;
        }

        if (name != null) {
            checkCerts(name, pd.getCodeSource());
        }

        return pd;
    }

    private String defineClassSourceLocation(ProtectionDomain pd) {
        CodeSource cs = pd.getCodeSource();
        String source = null;
        if (cs != null && cs.getLocation() != null) {
            source = cs.getLocation().toString();
        }
        return source;
    }

    private void postDefineClass(Class<?> c, ProtectionDomain pd) {

        getNamedPackage(c.getPackageName(), c.getModule());

        if (pd.getCodeSource() != null) {
            Certificate certs[] = pd.getCodeSource().getCertificates();
            if (certs != null)
                setSigners(c, certs);
        }
    }

    protected final Class<?> defineClass(String name, byte[] b, int off, int len,
                                         ProtectionDomain protectionDomain)
        throws ClassFormatError
    {
        protectionDomain = preDefineClass(name, protectionDomain);
        String source = defineClassSourceLocation(protectionDomain);
        Class<?> c = defineClass1(this, name, b, off, len, protectionDomain, source);
        postDefineClass(c, protectionDomain);
        return c;
    }

    protected final Class<?> defineClass(String name, java.nio.ByteBuffer b,
                                         ProtectionDomain protectionDomain)
        throws ClassFormatError
    {
        int len = b.remaining();

        if (!b.isDirect()) {
            if (b.hasArray()) {
                return defineClass(name, b.array(),
                                   b.position() + b.arrayOffset(), len,
                                   protectionDomain);
            } else {

                byte[] tb = new byte[len];
                b.get(tb);
                return defineClass(name, tb, 0, len, protectionDomain);
            }
        }

        protectionDomain = preDefineClass(name, protectionDomain);
        String source = defineClassSourceLocation(protectionDomain);
        Class<?> c = defineClass2(this, name, b, b.position(), len, protectionDomain, source);
        postDefineClass(c, protectionDomain);
        return c;
    }

    static native Class<?> defineClass1(ClassLoader loader, String name, byte[] b, int off, int len,
                                        ProtectionDomain pd, String source);

    static native Class<?> defineClass2(ClassLoader loader, String name, java.nio.ByteBuffer b,
                                        int off, int len, ProtectionDomain pd,
                                        String source);

    private boolean checkName(String name) {
        if ((name == null) || (name.isEmpty()))
            return true;
        if ((name.indexOf('/') != -1) || (name.charAt(0) == '['))
            return false;
        return true;
    }

    private void checkCerts(String name, CodeSource cs) {
        int i = name.lastIndexOf('.');
        String pname = (i == -1) ? "" : name.substring(0, i);

        Certificate[] certs = null;
        if (cs != null) {
            certs = cs.getCertificates();
        }
        Certificate[] pcerts = null;
        if (parallelLockMap == null) {
            synchronized (this) {
                pcerts = package2certs.get(pname);
                if (pcerts == null) {
                    package2certs.put(pname, (certs == null? nocerts:certs));
                }
            }
        } else {
            pcerts = ((ConcurrentHashMap<String, Certificate[]>)package2certs).
                putIfAbsent(pname, (certs == null? nocerts:certs));
        }
        if (pcerts != null && !compareCerts(pcerts, certs)) {
            throw new SecurityException("class \"" + name
                + "\"'s signer information does not match signer information"
                + " of other classes in the same package");
        }
    }

    private boolean compareCerts(Certificate[] pcerts,
                                 Certificate[] certs)
    {

        if ((certs == null) || (certs.length == 0)) {
            return pcerts.length == 0;
        }

        if (certs.length != pcerts.length)
            return false;

        boolean match;
        for (Certificate cert : certs) {
            match = false;
            for (Certificate pcert : pcerts) {
                if (cert.equals(pcert)) {
                    match = true;
                    break;
                }
            }
            if (!match) return false;
        }

        for (Certificate pcert : pcerts) {
            match = false;
            for (Certificate cert : certs) {
                if (pcert.equals(cert)) {
                    match = true;
                    break;
                }
            }
            if (!match) return false;
        }

        return true;
    }

    protected final void resolveClass(Class<?> c) {
        if (c == null) {
            throw new NullPointerException();
        }
    }

    protected final Class<?> findSystemClass(String name)
        throws ClassNotFoundException
    {
        return getSystemClassLoader().loadClass(name);
    }

    Class<?> findBootstrapClassOrNull(String name) {
        if (!checkName(name)) return null;

        return findBootstrapClass(name);
    }

    private native Class<?> findBootstrapClass(String name);

    protected final Class<?> findLoadedClass(String name) {
        if (!checkName(name))
            return null;
        return findLoadedClass0(name);
    }

    private final native Class<?> findLoadedClass0(String name);

    protected final void setSigners(Class<?> c, Object[] signers) {
        c.setSigners(signers);
    }

    protected URL findResource(String moduleName, String name) throws IOException {
        if (moduleName == null) {
            return findResource(name);
        } else {
            return null;
        }
    }

    public URL getResource(String name) {
        Objects.requireNonNull(name);
        URL url;
        if (parent != null) {
            url = parent.getResource(name);
        } else {
            url = BootLoader.findResource(name);
        }
        if (url == null) {
            url = findResource(name);
        }
        return url;
    }

    public Enumeration<URL> getResources(String name) throws IOException {
        Objects.requireNonNull(name);
        @SuppressWarnings("unchecked")
        Enumeration<URL>[] tmp = (Enumeration<URL>[]) new Enumeration<?>[2];
        if (parent != null) {
            tmp[0] = parent.getResources(name);
        } else {
            tmp[0] = BootLoader.findResources(name);
        }
        tmp[1] = findResources(name);

        return new CompoundEnumeration<>(tmp);
    }

    public Stream<URL> resources(String name) {
        Objects.requireNonNull(name);
        int characteristics = Spliterator.NONNULL | Spliterator.IMMUTABLE;
        Supplier<Spliterator<URL>> si = () -> {
            try {
                return Spliterators.spliteratorUnknownSize(
                    getResources(name).asIterator(), characteristics);
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        };
        return StreamSupport.stream(si, characteristics, false);
    }

    protected URL findResource(String name) {
        return null;
    }

    protected Enumeration<URL> findResources(String name) throws IOException {
        return Collections.emptyEnumeration();
    }

    @CallerSensitive
    protected static boolean registerAsParallelCapable() {
        Class<? extends ClassLoader> callerClass =
            Reflection.getCallerClass().asSubclass(ClassLoader.class);
        return ParallelLoaders.register(callerClass);
    }

    public final boolean isRegisteredAsParallelCapable() {
        return ParallelLoaders.isRegistered(this.getClass());
    }

    public static URL getSystemResource(String name) {
        return getSystemClassLoader().getResource(name);
    }

    public static Enumeration<URL> getSystemResources(String name)
        throws IOException
    {
        return getSystemClassLoader().getResources(name);
    }

    public InputStream getResourceAsStream(String name) {
        Objects.requireNonNull(name);
        URL url = getResource(name);
        try {
            return url != null ? url.openStream() : null;
        } catch (IOException e) {
            return null;
        }
    }

    public static InputStream getSystemResourceAsStream(String name) {
        URL url = getSystemResource(name);
        try {
            return url != null ? url.openStream() : null;
        } catch (IOException e) {
            return null;
        }
    }

    @CallerSensitive
    public final ClassLoader getParent() {
        if (parent == null)
            return null;
        SecurityManager sm = System.getSecurityManager();
        if (sm != null) {

            checkClassLoaderPermission(parent, Reflection.getCallerClass());
        }
        return parent;
    }

    public final Module getUnnamedModule() {
        return unnamedModule;
    }

    @CallerSensitive
    public static ClassLoader getPlatformClassLoader() {
        SecurityManager sm = System.getSecurityManager();
        ClassLoader loader = getBuiltinPlatformClassLoader();
        if (sm != null) {
            checkClassLoaderPermission(loader, Reflection.getCallerClass());
        }
        return loader;
    }

    @CallerSensitive
    public static ClassLoader getSystemClassLoader() {
        switch (VM.initLevel()) {
            case 0:
            case 1:
            case 2:

                return getBuiltinAppClassLoader();
            case 3:
                String msg = "getSystemClassLoader cannot be called during the system class loader instantiation";
                throw new IllegalStateException(msg);
            default:

                assert VM.isBooted() && scl != null;
                SecurityManager sm = System.getSecurityManager();
                if (sm != null) {
                    checkClassLoaderPermission(scl, Reflection.getCallerClass());
                }
                return scl;
        }
    }

    static ClassLoader getBuiltinPlatformClassLoader() {
        return ClassLoaders.platformClassLoader();
    }

    static ClassLoader getBuiltinAppClassLoader() {
        return ClassLoaders.appClassLoader();
    }

    static synchronized ClassLoader initSystemClassLoader() {
        if (VM.initLevel() != 3) {
            throw new InternalError("system class loader cannot be set at initLevel " +
                                    VM.initLevel());
        }

        if (scl != null) {
            throw new IllegalStateException("recursive invocation");
        }

        ClassLoader builtinLoader = getBuiltinAppClassLoader();

        String cn = System.getProperty("java.system.class.loader");
        if (cn != null) {
            try {

                Constructor<?> ctor = Class.forName(cn, false, builtinLoader)
                                           .getDeclaredConstructor(ClassLoader.class);
                scl = (ClassLoader) ctor.newInstance(builtinLoader);
            } catch (Exception e) {
                Throwable cause = e;
                if (e instanceof InvocationTargetException) {
                    cause = e.getCause();
                    if (cause instanceof Error) {
                        throw (Error) cause;
                    }
                }
                if (cause instanceof RuntimeException) {
                    throw (RuntimeException) cause;
                }
                throw new Error(cause.getMessage(), cause);
            }
        } else {
            scl = builtinLoader;
        }
        return scl;
    }

    static void initLibraryPaths() {
        usr_paths = initializePath("java.library.path");
        sys_paths = initializePath("sun.boot.library.path");
    }

    boolean isAncestor(ClassLoader cl) {
        ClassLoader acl = this;
        do {
            acl = acl.parent;
            if (cl == acl) {
                return true;
            }
        } while (acl != null);
        return false;
    }

    private static boolean needsClassLoaderPermissionCheck(ClassLoader from,
                                                           ClassLoader to)
    {
        if (from == to)
            return false;

        if (from == null)
            return false;

        return !to.isAncestor(from);
    }

    static ClassLoader getClassLoader(Class<?> caller) {

        if (caller == null) {
            return null;
        }

        return caller.getClassLoader0();
    }

    static void checkClassLoaderPermission(ClassLoader cl, Class<?> caller) {
        SecurityManager sm = System.getSecurityManager();
        if (sm != null) {

            ClassLoader ccl = getClassLoader(caller);
            if (needsClassLoaderPermissionCheck(ccl, cl)) {
                sm.checkPermission(SecurityConstants.GET_CLASSLOADER_PERMISSION);
            }
        }
    }

    private static volatile ClassLoader scl;

    Package definePackage(Class<?> c) {
        if (c.isPrimitive() || c.isArray()) {
            return null;
        }

        return definePackage(c.getPackageName(), c.getModule());
    }

    Package definePackage(String name, Module m) {
        if (name.isEmpty() && m.isNamed()) {
            throw new InternalError("unnamed package in  " + m);
        }

        NamedPackage pkg = packages.get(name);
        if (pkg instanceof Package)
            return (Package)pkg;

        return (Package)packages.compute(name, (n, p) -> toPackage(n, p, m));
    }

    private Package toPackage(String name, NamedPackage p, Module m) {

        if (p == null)
            return NamedPackage.toPackage(name, m);

        if (p instanceof Package)
            return (Package)p;

        return NamedPackage.toPackage(p.packageName(), p.module());
    }

    protected Package definePackage(String name, String specTitle,
                                    String specVersion, String specVendor,
                                    String implTitle, String implVersion,
                                    String implVendor, URL sealBase)
    {
        Objects.requireNonNull(name);

        Package p = new Package(name, specTitle, specVersion, specVendor,
                                implTitle, implVersion, implVendor,
                                sealBase, this);

        if (packages.putIfAbsent(name, p) != null)
            throw new IllegalArgumentException(name);

        return p;
    }

    public final Package getDefinedPackage(String name) {
        Objects.requireNonNull(name, "name cannot be null");

        NamedPackage p = packages.get(name);
        if (p == null)
            return null;

        return definePackage(name, p.module());
    }

    public final Package[] getDefinedPackages() {
        return packages().toArray(Package[]::new);
    }

    @Deprecated(since="9")
    protected Package getPackage(String name) {
        Package pkg = getDefinedPackage(name);
        if (pkg == null) {
            if (parent != null) {
                pkg = parent.getPackage(name);
            } else {
                pkg = BootLoader.getDefinedPackage(name);
            }
        }
        return pkg;
    }

    protected Package[] getPackages() {
        Stream<Package> pkgs = packages();
        ClassLoader ld = parent;
        while (ld != null) {
            pkgs = Stream.concat(ld.packages(), pkgs);
            ld = ld.parent;
        }
        return Stream.concat(BootLoader.packages(), pkgs)
                     .toArray(Package[]::new);
    }

    Stream<Package> packages() {
        return packages.values().stream()
                       .map(p -> definePackage(p.packageName(), p.module()));
    }

    protected String findLibrary(String libname) {
        return null;
    }

    static class NativeLibrary {
        private static final boolean loadLibraryOnlyIfPresent = ClassLoaderHelper.loadLibraryOnlyIfPresent();

        final Class<?> fromClass;

        final String name;

        final boolean isBuiltin;

        long handle;

        int jniVersion;

        native boolean load0(String name, boolean isBuiltin,
                             boolean throwExceptionIfFail);

        native long findEntry(String name);

        NativeLibrary(Class<?> fromClass, String name, boolean isBuiltin) {
            this.name = name;
            this.fromClass = fromClass;
            this.isBuiltin = isBuiltin;
        }

        boolean load() {
            if (handle != 0) {
                throw new InternalError("Native library " + name + " has been loaded");
            }

            if (!load0(name, isBuiltin, loadLibraryOnlyIfPresent)) return false;

            ClassLoader loader = fromClass.getClassLoader();
            if (loader != null &&
                loader != getBuiltinPlatformClassLoader() &&
                loader != getBuiltinAppClassLoader()) {
                CleanerFactory.cleaner().register(loader,
                        new Unloader(name, handle, isBuiltin));
            }
            return true;
        }

        static boolean loadLibrary(Class<?> fromClass, String name, boolean isBuiltin) {
            ClassLoader loader =
                fromClass == null ? null : fromClass.getClassLoader();

            synchronized (loadedLibraryNames) {
                Map<String, NativeLibrary> libs =
                    loader != null ? loader.nativeLibraries() : systemNativeLibraries();
                if (libs.containsKey(name)) {
                    return true;
                }

                if (loadedLibraryNames.contains(name)) {
                    throw new UnsatisfiedLinkError("Native Library " + name +
                        " already loaded in another classloader");
                }

                for (NativeLibrary lib : nativeLibraryContext) {
                    if (name.equals(lib.name)) {
                        if (loader == lib.fromClass.getClassLoader()) {
                            return true;
                        } else {
                            throw new UnsatisfiedLinkError("Native Library " +
                                name + " is being loaded in another classloader");
                        }
                    }
                }
                NativeLibrary lib = new NativeLibrary(fromClass, name, isBuiltin);

                nativeLibraryContext.push(lib);
                try {
                    if (!lib.load()) return false;
                } finally {
                    nativeLibraryContext.pop();
                }

                loadedLibraryNames.add(name);
                libs.put(name, lib);
            }
            return true;
        }

        static Class<?> getFromClass() {
            return nativeLibraryContext.peek().fromClass;
        }

        static Deque<NativeLibrary> nativeLibraryContext = new ArrayDeque<>(8);

        static class Unloader implements Runnable {

            static final NativeLibrary UNLOADER =
                new NativeLibrary(null, "dummy", false);
            final String name;
            final long handle;
            final boolean isBuiltin;

            Unloader(String name, long handle, boolean isBuiltin) {
                if (handle == 0) {
                    throw new IllegalArgumentException(
                        "Invalid handle for native library " + name);
                }

                this.name = name;
                this.handle = handle;
                this.isBuiltin = isBuiltin;
            }

            @Override
            public void run() {
                synchronized (loadedLibraryNames) {

                    loadedLibraryNames.remove(name);
                    nativeLibraryContext.push(UNLOADER);
                    try {
                        unload(name, isBuiltin, handle);
                    } finally {
                        nativeLibraryContext.pop();
                    }

                }
            }
        }

        static native void unload(String name, boolean isBuiltin, long handle);
    }

    private static String usr_paths[];
    private static String sys_paths[];

    private static String[] initializePath(String propName) {
        String ldPath = System.getProperty(propName, "");
        int ldLen = ldPath.length();
        char ps = File.pathSeparatorChar;
        int psCount = 0;

        if (ClassLoaderHelper.allowsQuotedPathElements &&
            ldPath.indexOf('\"') >= 0) {

            char[] buf = new char[ldLen];
            int bufLen = 0;
            for (int i = 0; i < ldLen; ++i) {
                char ch = ldPath.charAt(i);
                if (ch == '\"') {
                    while (++i < ldLen &&
                        (ch = ldPath.charAt(i)) != '\"') {
                        buf[bufLen++] = ch;
                    }
                } else {
                    if (ch == ps) {
                        psCount++;
                        ch = '\"';
                    }
                    buf[bufLen++] = ch;
                }
            }
            ldPath = new String(buf, 0, bufLen);
            ldLen = bufLen;
            ps = '\"';
        } else {
            for (int i = ldPath.indexOf(ps); i >= 0;
                 i = ldPath.indexOf(ps, i + 1)) {
                psCount++;
            }
        }

        String[] paths = new String[psCount + 1];
        int pathStart = 0;
        for (int j = 0; j < psCount; ++j) {
            int pathEnd = ldPath.indexOf(ps, pathStart);
            paths[j] = (pathStart < pathEnd) ?
                ldPath.substring(pathStart, pathEnd) : ".";
            pathStart = pathEnd + 1;
        }
        paths[psCount] = (pathStart < ldLen) ?
            ldPath.substring(pathStart, ldLen) : ".";
        return paths;
    }

    static void loadLibrary(Class<?> fromClass, String name,
                            boolean isAbsolute) {
        ClassLoader loader =
            (fromClass == null) ? null : fromClass.getClassLoader();
        assert sys_paths != null : "should be initialized at this point";
        assert usr_paths != null : "should be initialized at this point";

        if (isAbsolute) {
            if (loadLibrary0(fromClass, new File(name))) {
                return;
            }
            throw new UnsatisfiedLinkError("Can't load library: " + name);
        }
        if (loader != null) {
            String libfilename = loader.findLibrary(name);
            if (libfilename != null) {
                File libfile = new File(libfilename);
                if (!libfile.isAbsolute()) {
                    throw new UnsatisfiedLinkError(
                        "ClassLoader.findLibrary failed to return an absolute path: " + libfilename);
                }
                if (loadLibrary0(fromClass, libfile)) {
                    return;
                }
                throw new UnsatisfiedLinkError("Can't load " + libfilename);
            }
        }
        for (String sys_path : sys_paths) {
            File libfile = new File(sys_path, System.mapLibraryName(name));
            if (loadLibrary0(fromClass, libfile)) {
                return;
            }
            libfile = ClassLoaderHelper.mapAlternativeName(libfile);
            if (libfile != null && loadLibrary0(fromClass, libfile)) {
                return;
            }
        }
        if (loader != null) {
            for (String usr_path : usr_paths) {
                File libfile = new File(usr_path, System.mapLibraryName(name));
                if (loadLibrary0(fromClass, libfile)) {
                    return;
                }
                libfile = ClassLoaderHelper.mapAlternativeName(libfile);
                if (libfile != null && loadLibrary0(fromClass, libfile)) {
                    return;
                }
            }
        }

        throw new UnsatisfiedLinkError("no " + name +
            " in java.library.path: " + Arrays.toString(usr_paths));
    }

    private static native String findBuiltinLib(String name);

    private static boolean loadLibrary0(Class<?> fromClass, final File file) {

        String name = findBuiltinLib(file.getName());
        boolean isBuiltin = (name != null);
        if (!isBuiltin) {
            name = AccessController.doPrivileged(
                new PrivilegedAction<>() {
                    public String run() {
                        try {
                            if (NativeLibrary.loadLibraryOnlyIfPresent && !file.exists()) {
                                return null;
                            }
                            return file.getCanonicalPath();
                        } catch (IOException e) {
                            return null;
                        }
                    }
                });
            if (name == null) {
                return false;
            }
        }
        return NativeLibrary.loadLibrary(fromClass, name, isBuiltin);
    }

    private static long findNative(ClassLoader loader, String entryName) {
        Map<String, NativeLibrary> libs =
            loader != null ? loader.nativeLibraries() : systemNativeLibraries();
        if (libs.isEmpty())
            return 0;

        for (NativeLibrary lib : libs.values()) {
            long entry = lib.findEntry(entryName);
            if (entry != 0) return entry;
        }
        return 0;
    }

    private static final Set<String> loadedLibraryNames = new HashSet<>();

    private static volatile Map<String, NativeLibrary> systemNativeLibraries;

    private volatile Map<String, NativeLibrary> nativeLibraries;

    private static Map<String, NativeLibrary> systemNativeLibraries() {
        Map<String, NativeLibrary> libs = systemNativeLibraries;
        if (libs == null) {
            synchronized (loadedLibraryNames) {
                libs = systemNativeLibraries;
                if (libs == null) {
                    libs = systemNativeLibraries = new ConcurrentHashMap<>();
                }
            }
        }
        return libs;
    }

    private Map<String, NativeLibrary> nativeLibraries() {
        Map<String, NativeLibrary> libs = nativeLibraries;
        if (libs == null) {
            synchronized (loadedLibraryNames) {
                libs = nativeLibraries;
                if (libs == null) {
                    libs = nativeLibraries = new ConcurrentHashMap<>();
                }
            }
        }
        return libs;
    }

    final Object assertionLock;

    private boolean defaultAssertionStatus = false;

    private Map<String, Boolean> packageAssertionStatus = null;

    Map<String, Boolean> classAssertionStatus = null;

    public void setDefaultAssertionStatus(boolean enabled) {
        synchronized (assertionLock) {
            if (classAssertionStatus == null)
                initializeJavaAssertionMaps();

            defaultAssertionStatus = enabled;
        }
    }

    public void setPackageAssertionStatus(String packageName,
                                          boolean enabled) {
        synchronized (assertionLock) {
            if (packageAssertionStatus == null)
                initializeJavaAssertionMaps();

            packageAssertionStatus.put(packageName, enabled);
        }
    }

    public void setClassAssertionStatus(String className, boolean enabled) {
        synchronized (assertionLock) {
            if (classAssertionStatus == null)
                initializeJavaAssertionMaps();

            classAssertionStatus.put(className, enabled);
        }
    }

    public void clearAssertionStatus() {

        synchronized (assertionLock) {
            classAssertionStatus = new HashMap<>();
            packageAssertionStatus = new HashMap<>();
            defaultAssertionStatus = false;
        }
    }

    boolean desiredAssertionStatus(String className) {
        synchronized (assertionLock) {

            Boolean result = classAssertionStatus.get(className);
            if (result != null)
                return result.booleanValue();

            int dotIndex = className.lastIndexOf('.');
            if (dotIndex < 0) {
                result = packageAssertionStatus.get(null);
                if (result != null)
                    return result.booleanValue();
            }
            while(dotIndex > 0) {
                className = className.substring(0, dotIndex);
                result = packageAssertionStatus.get(className);
                if (result != null)
                    return result.booleanValue();
                dotIndex = className.lastIndexOf('.', dotIndex-1);
            }

            return defaultAssertionStatus;
        }
    }

    private void initializeJavaAssertionMaps() {

        classAssertionStatus = new HashMap<>();
        packageAssertionStatus = new HashMap<>();
        AssertionStatusDirectives directives = retrieveDirectives();

        for(int i = 0; i < directives.classes.length; i++)
            classAssertionStatus.put(directives.classes[i],
                                     directives.classEnabled[i]);

        for(int i = 0; i < directives.packages.length; i++)
            packageAssertionStatus.put(directives.packages[i],
                                       directives.packageEnabled[i]);

        defaultAssertionStatus = directives.deflt;
    }

    private static native AssertionStatusDirectives retrieveDirectives();

    ConcurrentHashMap<?, ?> createOrGetClassLoaderValueMap() {
        ConcurrentHashMap<?, ?> map = classLoaderValueMap;
        if (map == null) {
            map = new ConcurrentHashMap<>();
            boolean set = trySetObjectField("classLoaderValueMap", map);
            if (!set) {

                map = classLoaderValueMap;
            }
        }
        return map;
    }

    private volatile ConcurrentHashMap<?, ?> classLoaderValueMap;

    private boolean trySetObjectField(String name, Object obj) {
        Unsafe unsafe = Unsafe.getUnsafe();
        Class<?> k = ClassLoader.class;
        long offset;
        offset = unsafe.objectFieldOffset(k, name);
        return unsafe.compareAndSetObject(this, offset, null, obj);
    }
}

final class CompoundEnumeration<E> implements Enumeration<E> {
    private final Enumeration<E>[] enums;
    private int index;

    public CompoundEnumeration(Enumeration<E>[] enums) {
        this.enums = enums;
    }

    private boolean next() {
        while (index < enums.length) {
            if (enums[index] != null && enums[index].hasMoreElements()) {
                return true;
            }
            index++;
        }
        return false;
    }

    public boolean hasMoreElements() {
        return next();
    }

    public E nextElement() {
        if (!next()) {
            throw new NoSuchElementException();
        }
        return enums[index].nextElement();
    }
}
