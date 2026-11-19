package java.lang;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.Console;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintStream;
import java.io.UnsupportedEncodingException;
import java.lang.annotation.Annotation;
import java.lang.module.ModuleDescriptor;
import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.net.URI;
import java.nio.charset.CharacterCodingException;
import java.security.AccessControlContext;
import java.security.ProtectionDomain;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.nio.channels.Channel;
import java.nio.channels.spi.SelectorProvider;
import java.nio.charset.Charset;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Properties;
import java.util.PropertyPermission;
import java.util.ResourceBundle;
import java.util.function.Supplier;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Stream;

import jdk.internal.logger.LoggerFinderLoader.TemporaryLoggerFinder;
import jdk.internal.util.StaticProperty;
import jdk.internal.module.ModuleBootstrap;
import jdk.internal.module.ServicesCatalog;
import jdk.internal.reflect.CallerSensitive;
import jdk.internal.reflect.Reflection;
import jdk.internal.HotSpotIntrinsicCandidate;
import jdk.internal.misc.JavaLangAccess;
import jdk.internal.misc.SharedSecrets;
import jdk.internal.misc.VM;
import jdk.internal.logger.LoggerFinderLoader;
import jdk.internal.logger.LazyLoggers;
import jdk.internal.logger.LocalizedLoggerWrapper;
import sun.nio.fs.DefaultFileSystemProvider;
import sun.reflect.annotation.AnnotationType;
import sun.nio.ch.Interruptible;
import sun.security.util.SecurityConstants;

public final class System {

    private static native void registerNatives();
    static {
        registerNatives();
    }

    private System() {
    }

    public static final InputStream in = null;

    public static final PrintStream out = null;

    public static final PrintStream err = null;

    private static volatile SecurityManager security;

    public static void setIn(InputStream in) {
        checkIO();
        setIn0(in);
    }

    public static void setOut(PrintStream out) {
        checkIO();
        setOut0(out);
    }

    public static void setErr(PrintStream err) {
        checkIO();
        setErr0(err);
    }

    private static volatile Console cons;

     public static Console console() {
         Console c;
         if ((c = cons) == null) {
             synchronized (System.class) {
                 if ((c = cons) == null) {
                     cons = c = SharedSecrets.getJavaIOAccess().console();
                 }
             }
         }
         return c;
     }

    public static Channel inheritedChannel() throws IOException {
        return SelectorProvider.provider().inheritedChannel();
    }

    private static void checkIO() {
        SecurityManager sm = getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new RuntimePermission("setIO"));
        }
    }

    private static native void setIn0(InputStream in);
    private static native void setOut0(PrintStream out);
    private static native void setErr0(PrintStream err);

    public static void setSecurityManager(final SecurityManager s) {
        if (security == null) {

            Object.class.getResource("java/lang/ANY");

            DefaultFileSystemProvider.theFileSystem();
        }
        if (s != null) {
            try {
                s.checkPackageAccess("java.lang");
            } catch (Exception e) {

            }
        }
        setSecurityManager0(s);
    }

    private static synchronized
    void setSecurityManager0(final SecurityManager s) {
        SecurityManager sm = getSecurityManager();
        if (sm != null) {

            sm.checkPermission(new RuntimePermission
                                     ("setSecurityManager"));
        }

        if ((s != null) && (s.getClass().getClassLoader() != null)) {

            AccessController.doPrivileged(new PrivilegedAction<>() {
                public Object run() {
                    s.getClass().getProtectionDomain().implies
                        (SecurityConstants.ALL_PERMISSION);
                    return null;
                }
            });
        }

        security = s;
    }

    public static SecurityManager getSecurityManager() {
        return security;
    }

    @HotSpotIntrinsicCandidate
    public static native long currentTimeMillis();

    @HotSpotIntrinsicCandidate
    public static native long nanoTime();

    @HotSpotIntrinsicCandidate
    public static native void arraycopy(Object src,  int  srcPos,
                                        Object dest, int destPos,
                                        int length);

    @HotSpotIntrinsicCandidate
    public static native int identityHashCode(Object x);

    private static Properties props;
    private static native Properties initProperties(Properties props);

    public static Properties getProperties() {
        SecurityManager sm = getSecurityManager();
        if (sm != null) {
            sm.checkPropertiesAccess();
        }

        return props;
    }

    public static String lineSeparator() {
        return lineSeparator;
    }

    private static String lineSeparator;

    public static void setProperties(Properties props) {
        SecurityManager sm = getSecurityManager();
        if (sm != null) {
            sm.checkPropertiesAccess();
        }
        if (props == null) {
            props = new Properties();
            initProperties(props);
            System.props = props;
            VersionProps.init();
        } else {
            System.props = props;
        }
    }

    public static String getProperty(String key) {
        checkKey(key);
        SecurityManager sm = getSecurityManager();
        if (sm != null) {
            sm.checkPropertyAccess(key);
        }

        return props.getProperty(key);
    }

    public static String getProperty(String key, String def) {
        checkKey(key);
        SecurityManager sm = getSecurityManager();
        if (sm != null) {
            sm.checkPropertyAccess(key);
        }

        return props.getProperty(key, def);
    }

    public static String setProperty(String key, String value) {
        checkKey(key);
        SecurityManager sm = getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new PropertyPermission(key,
                SecurityConstants.PROPERTY_WRITE_ACTION));
        }

        return (String) props.setProperty(key, value);
    }

    public static String clearProperty(String key) {
        checkKey(key);
        SecurityManager sm = getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new PropertyPermission(key, "write"));
        }

        return (String) props.remove(key);
    }

    private static void checkKey(String key) {
        if (key == null) {
            throw new NullPointerException("key can't be null");
        }
        if (key.equals("")) {
            throw new IllegalArgumentException("key can't be empty");
        }
    }

    public static String getenv(String name) {
        SecurityManager sm = getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new RuntimePermission("getenv."+name));
        }

        return ProcessEnvironment.getenv(name);
    }

    public static java.util.Map<String,String> getenv() {
        SecurityManager sm = getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new RuntimePermission("getenv.*"));
        }

        return ProcessEnvironment.getenv();
    }

    public interface Logger {

        public enum Level {

            ALL(Integer.MIN_VALUE),

            TRACE(400),

            DEBUG(500),

            INFO(800),

            WARNING(900),

            ERROR(1000),

            OFF(Integer.MAX_VALUE);

            private final int severity;

            private Level(int severity) {
                this.severity = severity;
            }

            public final String getName() {
                return name();
            }

            public final int getSeverity() {
                return severity;
            }
        }

        public String getName();

        public boolean isLoggable(Level level);

        public default void log(Level level, String msg) {
            log(level, (ResourceBundle) null, msg, (Object[]) null);
        }

        public default void log(Level level, Supplier<String> msgSupplier) {
            Objects.requireNonNull(msgSupplier);
            if (isLoggable(Objects.requireNonNull(level))) {
                log(level, (ResourceBundle) null, msgSupplier.get(), (Object[]) null);
            }
        }

        public default void log(Level level, Object obj) {
            Objects.requireNonNull(obj);
            if (isLoggable(Objects.requireNonNull(level))) {
                this.log(level, (ResourceBundle) null, obj.toString(), (Object[]) null);
            }
        }

        public default void log(Level level, String msg, Throwable thrown) {
            this.log(level, null, msg, thrown);
        }

        public default void log(Level level, Supplier<String> msgSupplier,
                Throwable thrown) {
            Objects.requireNonNull(msgSupplier);
            if (isLoggable(Objects.requireNonNull(level))) {
                this.log(level, null, msgSupplier.get(), thrown);
            }
        }

        public default void log(Level level, String format, Object... params) {
            this.log(level, null, format, params);
        }

        public void log(Level level, ResourceBundle bundle, String msg,
                Throwable thrown);

        public void log(Level level, ResourceBundle bundle, String format,
                Object... params);
    }

    public static abstract class LoggerFinder {

        static final RuntimePermission LOGGERFINDER_PERMISSION =
                new RuntimePermission("loggerFinder");

        protected LoggerFinder() {
            this(checkPermission());
        }

        private LoggerFinder(Void unused) {

        }

        private static Void checkPermission() {
            final SecurityManager sm = System.getSecurityManager();
            if (sm != null) {
                sm.checkPermission(LOGGERFINDER_PERMISSION);
            }
            return null;
        }

        public abstract Logger getLogger(String name, Module module);

        public Logger getLocalizedLogger(String name, ResourceBundle bundle,
                                         Module module) {
            return new LocalizedLoggerWrapper<>(getLogger(name, module), bundle);
        }

        public static LoggerFinder getLoggerFinder() {
            final SecurityManager sm = System.getSecurityManager();
            if (sm != null) {
                sm.checkPermission(LOGGERFINDER_PERMISSION);
            }
            return accessProvider();
        }

        private static volatile LoggerFinder service;
        static LoggerFinder accessProvider() {

            LoggerFinder finder = service;
            if (finder == null) {
                PrivilegedAction<LoggerFinder> pa =
                        () -> LoggerFinderLoader.getLoggerFinder();
                finder = AccessController.doPrivileged(pa, null,
                        LOGGERFINDER_PERMISSION);
                if (finder instanceof TemporaryLoggerFinder) return finder;
                service = finder;
            }
            return finder;
        }

    }

    @CallerSensitive
    public static Logger getLogger(String name) {
        Objects.requireNonNull(name);
        final Class<?> caller = Reflection.getCallerClass();
        if (caller == null) {
            throw new IllegalCallerException("no caller frame");
        }
        return LazyLoggers.getLogger(name, caller.getModule());
    }

    @CallerSensitive
    public static Logger getLogger(String name, ResourceBundle bundle) {
        final ResourceBundle rb = Objects.requireNonNull(bundle);
        Objects.requireNonNull(name);
        final Class<?> caller = Reflection.getCallerClass();
        if (caller == null) {
            throw new IllegalCallerException("no caller frame");
        }
        final SecurityManager sm = System.getSecurityManager();

        if (sm != null) {
            final PrivilegedAction<Logger> pa =
                    () -> LoggerFinder.accessProvider()
                            .getLocalizedLogger(name, rb, caller.getModule());
            return AccessController.doPrivileged(pa, null,
                                         LoggerFinder.LOGGERFINDER_PERMISSION);
        }
        return LoggerFinder.accessProvider()
                .getLocalizedLogger(name, rb, caller.getModule());
    }

    public static void exit(int status) {
        Runtime.getRuntime().exit(status);
    }

    public static void gc() {
        Runtime.getRuntime().gc();
    }

    public static void runFinalization() {
        Runtime.getRuntime().runFinalization();
    }

    @CallerSensitive
    public static void load(String filename) {
        Runtime.getRuntime().load0(Reflection.getCallerClass(), filename);
    }

    @CallerSensitive
    public static void loadLibrary(String libname) {
        Runtime.getRuntime().loadLibrary0(Reflection.getCallerClass(), libname);
    }

    public static native String mapLibraryName(String libname);

    private static PrintStream newPrintStream(FileOutputStream fos, String enc) {
       if (enc != null) {
            try {
                return new PrintStream(new BufferedOutputStream(fos, 128), true, enc);
            } catch (UnsupportedEncodingException uee) {}
        }
        return new PrintStream(new BufferedOutputStream(fos, 128), true);
    }

    private static void logInitException(boolean printToStderr,
                                         boolean printStackTrace,
                                         String msg,
                                         Throwable e) {
        if (VM.initLevel() < 1) {
            throw new InternalError("system classes not initialized");
        }
        PrintStream log = (printToStderr) ? err : out;
        if (msg != null) {
            log.println(msg);
        }
        if (printStackTrace) {
            e.printStackTrace(log);
        } else {
            log.println(e);
            for (Throwable suppressed : e.getSuppressed()) {
                log.println("Suppressed: " + suppressed);
            }
            Throwable cause = e.getCause();
            if (cause != null) {
                log.println("Caused by: " + cause);
            }
        }
    }

    private static void initPhase1() {

        props = new Properties(84);
        initProperties(props);

        VM.saveAndRemoveProperties(props);

        lineSeparator = props.getProperty("line.separator");
        StaticProperty.javaHome();
        VersionProps.init();

        FileInputStream fdIn = new FileInputStream(FileDescriptor.in);
        FileOutputStream fdOut = new FileOutputStream(FileDescriptor.out);
        FileOutputStream fdErr = new FileOutputStream(FileDescriptor.err);
        setIn0(new BufferedInputStream(fdIn));
        setOut0(newPrintStream(fdOut, props.getProperty("sun.stdout.encoding")));
        setErr0(newPrintStream(fdErr, props.getProperty("sun.stderr.encoding")));

        Terminator.setup();

        VM.initializeOSEnvironment();

        Thread current = Thread.currentThread();
        current.getThreadGroup().add(current);

        setJavaLangAccess();

        ClassLoader.initLibraryPaths();

        VM.initLevel(1);
    }

    static ModuleLayer bootLayer;

    private static int initPhase2(boolean printToStderr, boolean printStackTrace) {
        try {
            bootLayer = ModuleBootstrap.boot();
        } catch (Exception | Error e) {
            logInitException(printToStderr, printStackTrace,
                             "Error occurred during initialization of boot layer", e);
            return -1;
        }

        VM.initLevel(2);

        return 0;
    }

    private static void initPhase3() {

        String cn = System.getProperty("java.security.manager");
        if (cn != null) {
            if (cn.isEmpty() || "default".equals(cn)) {
                System.setSecurityManager(new SecurityManager());
            } else {
                try {
                    Class<?> c = Class.forName(cn, false, ClassLoader.getBuiltinAppClassLoader());
                    Constructor<?> ctor = c.getConstructor();

                    if (!SecurityManager.class.isAssignableFrom(c) ||
                            !Modifier.isPublic(c.getModifiers()) ||
                            !Modifier.isPublic(ctor.getModifiers())) {
                        throw new Error("Could not create SecurityManager: " + ctor.toString());
                    }

                    ctor.setAccessible(true);
                    SecurityManager sm = (SecurityManager) ctor.newInstance();
                    System.setSecurityManager(sm);
                } catch (Exception e) {
                    throw new Error("Could not create SecurityManager", e);
                }
            }
        }

        VM.initLevel(3);

        ClassLoader scl = ClassLoader.initSystemClassLoader();

        Thread.currentThread().setContextClassLoader(scl);

        VM.initLevel(4);
    }

    private static void setJavaLangAccess() {

        SharedSecrets.setJavaLangAccess(new JavaLangAccess() {
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
            public <E extends Enum<E>>
            E[] getEnumConstantsShared(Class<E> klass) {
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

            public String newStringNoRepl(byte[] bytes, Charset cs) throws CharacterCodingException  {
                return StringCoding.newStringNoRepl(bytes, cs);
            }

            public byte[] getBytesNoRepl(String s, Charset cs) throws CharacterCodingException {
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
        });
    }
}
