package java.lang;

import java.io.IOException;
import java.io.InputStream;
import java.lang.annotation.Annotation;
import java.lang.module.Configuration;
import java.lang.module.ModuleReference;
import java.lang.module.ModuleDescriptor;
import java.lang.module.ModuleDescriptor.Exports;
import java.lang.module.ModuleDescriptor.Opens;
import java.lang.module.ModuleDescriptor.Version;
import java.lang.module.ResolvedModule;
import java.lang.reflect.AnnotatedElement;
import java.net.URI;
import java.net.URL;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import jdk.internal.loader.BuiltinClassLoader;
import jdk.internal.loader.BootLoader;
import jdk.internal.loader.ClassLoaders;
import jdk.internal.module.IllegalAccessLogger;
import jdk.internal.module.ModuleLoaderMap;
import jdk.internal.module.ServicesCatalog;
import jdk.internal.module.Resources;
import jdk.internal.org.objectweb.asm.AnnotationVisitor;
import jdk.internal.org.objectweb.asm.Attribute;
import jdk.internal.org.objectweb.asm.ClassReader;
import jdk.internal.org.objectweb.asm.ClassVisitor;
import jdk.internal.org.objectweb.asm.ClassWriter;
import jdk.internal.org.objectweb.asm.ModuleVisitor;
import jdk.internal.org.objectweb.asm.Opcodes;
import jdk.internal.reflect.CallerSensitive;
import jdk.internal.reflect.Reflection;
import sun.security.util.SecurityConstants;

public final class Module implements AnnotatedElement {

    private final ModuleLayer layer;

    private final String name;
    private final ClassLoader loader;

    private final ModuleDescriptor descriptor;

    Module(ModuleLayer layer,
           ClassLoader loader,
           ModuleDescriptor descriptor,
           URI uri)
    {
        this.layer = layer;
        this.name = descriptor.name();
        this.loader = loader;
        this.descriptor = descriptor;

        boolean isOpen = descriptor.isOpen() || descriptor.isAutomatic();
        Version version = descriptor.version().orElse(null);
        String vs = Objects.toString(version, null);
        String loc = Objects.toString(uri, null);
        String[] packages = descriptor.packages().toArray(new String[0]);
        defineModule0(this, isOpen, vs, loc, packages);
    }

    Module(ClassLoader loader) {
        this.layer = null;
        this.name = null;
        this.loader = loader;
        this.descriptor = null;
    }

    Module(ClassLoader loader, ModuleDescriptor descriptor) {
        this.layer = null;
        this.name = descriptor.name();
        this.loader = loader;
        this.descriptor = descriptor;
    }

    public boolean isNamed() {
        return name != null;
    }

    public String getName() {
        return name;
    }

    public ClassLoader getClassLoader() {
        SecurityManager sm = System.getSecurityManager();
        if (sm != null) {
            sm.checkPermission(SecurityConstants.GET_CLASSLOADER_PERMISSION);
        }
        return loader;
    }

    public ModuleDescriptor getDescriptor() {
        return descriptor;
    }

    public ModuleLayer getLayer() {
        if (isNamed()) {
            ModuleLayer layer = this.layer;
            if (layer != null)
                return layer;

            if (loader == null && name.equals("java.base")) {
                return ModuleLayer.boot();
            }
        }
        return null;
    }

    private static final Module ALL_UNNAMED_MODULE = new Module(null);
    private static final Set<Module> ALL_UNNAMED_MODULE_SET = Set.of(ALL_UNNAMED_MODULE);

    private static final Module EVERYONE_MODULE = new Module(null);
    private static final Set<Module> EVERYONE_SET = Set.of(EVERYONE_MODULE);

    private static class ReflectionData {

        static final WeakPairMap<Module, Module, Boolean> reads =
            new WeakPairMap<>();

        static final WeakPairMap<Module, Module, Map<String, Boolean>> exports =
            new WeakPairMap<>();

        static final WeakPairMap<Module, Class<?>, Boolean> uses =
            new WeakPairMap<>();
    }

    private volatile Set<Module> reads;

    public boolean canRead(Module other) {
        Objects.requireNonNull(other);

        if (!this.isNamed())
            return true;

        if (other == this)
            return true;

        if (other.isNamed()) {
            Set<Module> reads = this.reads;
            if (reads != null && reads.contains(other))
                return true;
        }

        if (ReflectionData.reads.containsKeyPair(this, other))
            return true;

        if (!other.isNamed()
            && ReflectionData.reads.containsKeyPair(this, ALL_UNNAMED_MODULE))
            return true;

        return false;
    }

    @CallerSensitive
    public Module addReads(Module other) {
        Objects.requireNonNull(other);
        if (this.isNamed()) {
            Module caller = getCallerModule(Reflection.getCallerClass());
            if (caller != this) {
                throw new IllegalCallerException(caller + " != " + this);
            }
            implAddReads(other, true);
        }
        return this;
    }

    void implAddReads(Module other) {
        implAddReads(other, true);
    }

    void implAddReadsAllUnnamed() {
        implAddReads(Module.ALL_UNNAMED_MODULE, true);
    }

    void implAddReadsNoSync(Module other) {
        implAddReads(other, false);
    }

    private void implAddReads(Module other, boolean syncVM) {
        Objects.requireNonNull(other);
        if (!canRead(other)) {

            if (syncVM) {
                if (other == ALL_UNNAMED_MODULE) {
                    addReads0(this, null);
                } else {
                    addReads0(this, other);
                }
            }

            ReflectionData.reads.putIfAbsent(this, other, Boolean.TRUE);
        }
    }

    private volatile Map<String, Set<Module>> openPackages;

    private volatile Map<String, Set<Module>> exportedPackages;

    public boolean isExported(String pn, Module other) {
        Objects.requireNonNull(pn);
        Objects.requireNonNull(other);
        return implIsExportedOrOpen(pn, other, false);
    }

    public boolean isOpen(String pn, Module other) {
        Objects.requireNonNull(pn);
        Objects.requireNonNull(other);
        return implIsExportedOrOpen(pn, other, true);
    }

    public boolean isExported(String pn) {
        Objects.requireNonNull(pn);
        return implIsExportedOrOpen(pn, EVERYONE_MODULE, false);
    }

    public boolean isOpen(String pn) {
        Objects.requireNonNull(pn);
        return implIsExportedOrOpen(pn, EVERYONE_MODULE, true);
    }

    private boolean implIsExportedOrOpen(String pn, Module other, boolean open) {

        if (!isNamed())
            return true;

        if (other == this && descriptor.packages().contains(pn))
            return true;

        if (descriptor.isOpen() || descriptor.isAutomatic())
            return descriptor.packages().contains(pn);

        if (isStaticallyExportedOrOpen(pn, other, open))
            return true;

        if (isReflectivelyExportedOrOpen(pn, other, open))
            return true;

        return false;
    }

    private boolean isStaticallyExportedOrOpen(String pn, Module other, boolean open) {

        Map<String, Set<Module>> openPackages = this.openPackages;
        if (openPackages != null && allows(openPackages.get(pn), other)) {
            return true;
        }

        if (!open) {

            Map<String, Set<Module>> exportedPackages = this.exportedPackages;
            if (exportedPackages != null && allows(exportedPackages.get(pn), other)) {
                return true;
            }
        }

        return false;
    }

    private boolean allows(Set<Module> targets, Module module) {
       if (targets != null) {
           if (targets.contains(EVERYONE_MODULE))
               return true;
           if (module != EVERYONE_MODULE) {
               if (targets.contains(module))
                   return true;
               if (!module.isNamed() && targets.contains(ALL_UNNAMED_MODULE))
                   return true;
           }
        }
        return false;
    }

    private boolean isReflectivelyExportedOrOpen(String pn, Module other, boolean open) {

        Map<String, Boolean> exports = ReflectionData.exports.get(this, EVERYONE_MODULE);
        if (exports != null) {
            Boolean b = exports.get(pn);
            if (b != null) {
                boolean isOpen = b.booleanValue();
                if (!open || isOpen) return true;
            }
        }

        if (other != EVERYONE_MODULE) {

            exports = ReflectionData.exports.get(this, other);
            if (exports != null) {
                Boolean b = exports.get(pn);
                if (b != null) {
                    boolean isOpen = b.booleanValue();
                    if (!open || isOpen) return true;
                }
            }

            if (!other.isNamed()) {
                exports = ReflectionData.exports.get(this, ALL_UNNAMED_MODULE);
                if (exports != null) {
                    Boolean b = exports.get(pn);
                    if (b != null) {
                        boolean isOpen = b.booleanValue();
                        if (!open || isOpen) return true;
                    }
                }
            }

        }

        return false;
    }

    boolean isReflectivelyExported(String pn, Module other) {
        return isReflectivelyExportedOrOpen(pn, other, false);
    }

    boolean isReflectivelyOpened(String pn, Module other) {
        return isReflectivelyExportedOrOpen(pn, other, true);
    }

    @CallerSensitive
    public Module addExports(String pn, Module other) {
        if (pn == null)
            throw new IllegalArgumentException("package is null");
        Objects.requireNonNull(other);

        if (isNamed()) {
            Module caller = getCallerModule(Reflection.getCallerClass());
            if (caller != this) {
                throw new IllegalCallerException(caller + " != " + this);
            }
            implAddExportsOrOpens(pn, other, false, true);
        }

        return this;
    }

    @CallerSensitive
    public Module addOpens(String pn, Module other) {
        if (pn == null)
            throw new IllegalArgumentException("package is null");
        Objects.requireNonNull(other);

        if (isNamed()) {
            Module caller = getCallerModule(Reflection.getCallerClass());
            if (caller != this && (caller == null || !isOpen(pn, caller)))
                throw new IllegalCallerException(pn + " is not open to " + caller);
            implAddExportsOrOpens(pn, other, true, true);
        }

        return this;
    }

    void implAddExports(String pn) {
        implAddExportsOrOpens(pn, Module.EVERYONE_MODULE, false, true);
    }

    void implAddExports(String pn, Module other) {
        implAddExportsOrOpens(pn, other, false, true);
    }

    void implAddExportsToAllUnnamed(String pn) {
        implAddExportsOrOpens(pn, Module.ALL_UNNAMED_MODULE, false, true);
    }

    void implAddExportsNoSync(String pn) {
        implAddExportsOrOpens(pn.replace('/', '.'), Module.EVERYONE_MODULE, false, false);
    }

    void implAddExportsNoSync(String pn, Module other) {
        implAddExportsOrOpens(pn.replace('/', '.'), other, false, false);
    }

    void implAddOpens(String pn) {
        implAddExportsOrOpens(pn, Module.EVERYONE_MODULE, true, true);
    }

    void implAddOpens(String pn, Module other) {
        implAddExportsOrOpens(pn, other, true, true);
    }

    void implAddOpensToAllUnnamed(String pn) {
        implAddExportsOrOpens(pn, Module.ALL_UNNAMED_MODULE, true, true);
    }

    private void implAddExportsOrOpens(String pn,
                                       Module other,
                                       boolean open,
                                       boolean syncVM) {
        Objects.requireNonNull(other);
        Objects.requireNonNull(pn);

        if (!isNamed() || descriptor.isOpen() || descriptor.isAutomatic())
            return;

        if (implIsExportedOrOpen(pn, other, open)) {

            boolean needToAdd = false;
            if (!other.isNamed()) {
                IllegalAccessLogger l = IllegalAccessLogger.illegalAccessLogger();
                if (l != null) {
                    if (open) {
                        needToAdd = l.isOpenForIllegalAccess(this, pn);
                    } else {
                        needToAdd = l.isExportedForIllegalAccess(this, pn);
                    }
                }
            }
            if (!needToAdd) {

                return;
            }
        }

        if (!descriptor.packages().contains(pn)) {
            throw new IllegalArgumentException("package " + pn
                                               + " not in contents");
        }

        if (syncVM) {
            if (other == EVERYONE_MODULE) {
                addExportsToAll0(this, pn);
            } else if (other == ALL_UNNAMED_MODULE) {
                addExportsToAllUnnamed0(this, pn);
            } else {
                addExports0(this, pn, other);
            }
        }

        Map<String, Boolean> map = ReflectionData.exports
            .computeIfAbsent(this, other,
                             (m1, m2) -> new ConcurrentHashMap<>());
        if (open) {
            map.put(pn, Boolean.TRUE);
        } else {
            map.putIfAbsent(pn, Boolean.FALSE);
        }
    }

    void implAddOpensToAllUnnamed(Iterator<String> iterator) {
        if (jdk.internal.misc.VM.isModuleSystemInited()) {
            throw new IllegalStateException("Module system already initialized");
        }

        Map<String, Set<Module>> openPackages = this.openPackages;
        if (openPackages == null) {
            openPackages = new HashMap<>();
        } else {
            openPackages = new HashMap<>(openPackages);
        }
        while (iterator.hasNext()) {
            String pn = iterator.next();
            Set<Module> prev = openPackages.putIfAbsent(pn, ALL_UNNAMED_MODULE_SET);
            if (prev != null) {
                prev.add(ALL_UNNAMED_MODULE);
            }

            addExportsToAllUnnamed0(this, pn);
        }
        this.openPackages = openPackages;
    }

    @CallerSensitive
    public Module addUses(Class<?> service) {
        Objects.requireNonNull(service);

        if (isNamed() && !descriptor.isAutomatic()) {
            Module caller = getCallerModule(Reflection.getCallerClass());
            if (caller != this) {
                throw new IllegalCallerException(caller + " != " + this);
            }
            implAddUses(service);
        }

        return this;
    }

    void implAddUses(Class<?> service) {
        if (!canUse(service)) {
            ReflectionData.uses.putIfAbsent(this, service, Boolean.TRUE);
        }
    }

    public boolean canUse(Class<?> service) {
        Objects.requireNonNull(service);

        if (!isNamed())
            return true;

        if (descriptor.isAutomatic())
            return true;

        if (descriptor.uses().contains(service.getName()))
            return true;

        return ReflectionData.uses.containsKeyPair(this, service);
    }

    public Set<String> getPackages() {
        if (isNamed()) {
            return descriptor.packages();
        } else {

            Stream<Package> packages;
            if (loader == null) {
                packages = BootLoader.packages();
            } else {
                packages = loader.packages();
            }
            return packages.map(Package::getName).collect(Collectors.toSet());
        }
    }

    static Map<String, Module> defineModules(Configuration cf,
                                             Function<String, ClassLoader> clf,
                                             ModuleLayer layer)
    {
        boolean isBootLayer = (ModuleLayer.boot() == null);

        int cap = (int)(cf.modules().size() / 0.75f + 1.0f);
        Map<String, Module> nameToModule = new HashMap<>(cap);
        Map<String, ClassLoader> nameToLoader = new HashMap<>(cap);

        Set<ClassLoader> loaders = new HashSet<>();
        boolean hasPlatformModules = false;

        for (ResolvedModule resolvedModule : cf.modules()) {
            String name = resolvedModule.name();
            ClassLoader loader = clf.apply(name);
            nameToLoader.put(name, loader);
            if (loader == null || loader == ClassLoaders.platformClassLoader()) {
                if (!(clf instanceof ModuleLoaderMap.Mapper)) {
                    throw new IllegalArgumentException("loader can't be 'null'"
                            + " or the platform class loader");
                }
                hasPlatformModules = true;
            } else {
                loaders.add(loader);
            }
        }

        for (ResolvedModule resolvedModule : cf.modules()) {
            ModuleReference mref = resolvedModule.reference();
            ModuleDescriptor descriptor = mref.descriptor();
            String name = descriptor.name();
            ClassLoader loader = nameToLoader.get(name);
            Module m;
            if (loader == null && name.equals("java.base")) {

                m = Object.class.getModule();
            } else {
                URI uri = mref.location().orElse(null);
                m = new Module(layer, loader, descriptor, uri);
            }
            nameToModule.put(name, m);
        }

        for (ResolvedModule resolvedModule : cf.modules()) {
            ModuleReference mref = resolvedModule.reference();
            ModuleDescriptor descriptor = mref.descriptor();

            String mn = descriptor.name();
            Module m = nameToModule.get(mn);
            assert m != null;

            Set<Module> reads = new HashSet<>();

            Map<String, Module> nameToSource = Map.of();

            for (ResolvedModule other : resolvedModule.reads()) {
                Module m2 = null;
                if (other.configuration() == cf) {

                    m2 = nameToModule.get(other.name());
                    assert m2 != null;
                } else {

                    for (ModuleLayer parent: layer.parents()) {
                        m2 = findModule(parent, other);
                        if (m2 != null)
                            break;
                    }
                    assert m2 != null;
                    if (nameToSource.isEmpty())
                        nameToSource = new HashMap<>();
                    nameToSource.put(other.name(), m2);
                }
                reads.add(m2);

                addReads0(m, m2);
            }
            m.reads = reads;

            if (descriptor.isAutomatic()) {
                m.implAddReads(ALL_UNNAMED_MODULE, true);
            }

            if (!descriptor.isOpen() && !descriptor.isAutomatic()) {
                if (isBootLayer && descriptor.opens().isEmpty()) {

                    initExports(m, nameToModule);
                } else {
                    initExportsAndOpens(m, nameToSource, nameToModule, layer.parents());
                }
            }
        }

        if (hasPlatformModules) {
            ClassLoader pcl = ClassLoaders.platformClassLoader();
            ServicesCatalog bootCatalog = BootLoader.getServicesCatalog();
            ServicesCatalog pclCatalog = ServicesCatalog.getServicesCatalog(pcl);
            for (ResolvedModule resolvedModule : cf.modules()) {
                ModuleReference mref = resolvedModule.reference();
                ModuleDescriptor descriptor = mref.descriptor();
                if (!descriptor.provides().isEmpty()) {
                    String name = descriptor.name();
                    Module m = nameToModule.get(name);
                    ClassLoader loader = nameToLoader.get(name);
                    if (loader == null) {
                        bootCatalog.register(m);
                    } else if (loader == pcl) {
                        pclCatalog.register(m);
                    }
                }
            }
        }

        for (ClassLoader loader : loaders) {
            layer.bindToLoader(loader);
        }

        return nameToModule;
    }

    private static Module findModule(ModuleLayer parent,
                                     ResolvedModule resolvedModule) {
        Configuration cf = resolvedModule.configuration();
        String dn = resolvedModule.name();
        return parent.layers()
                .filter(l -> l.configuration() == cf)
                .findAny()
                .map(layer -> {
                    Optional<Module> om = layer.findModule(dn);
                    assert om.isPresent() : dn + " not found in layer";
                    Module m = om.get();
                    assert m.getLayer() == layer : m + " not in expected layer";
                    return m;
                })
                .orElse(null);
    }

    private static void initExports(Module m, Map<String, Module> nameToModule) {
        Map<String, Set<Module>> exportedPackages = new HashMap<>();

        for (Exports exports : m.getDescriptor().exports()) {
            String source = exports.source();
            if (exports.isQualified()) {

                Set<Module> targets = new HashSet<>();
                for (String target : exports.targets()) {
                    Module m2 = nameToModule.get(target);
                    if (m2 != null) {
                        addExports0(m, source, m2);
                        targets.add(m2);
                    }
                }
                if (!targets.isEmpty()) {
                    exportedPackages.put(source, targets);
                }
            } else {

                addExportsToAll0(m, source);
                exportedPackages.put(source, EVERYONE_SET);
            }
        }

        if (!exportedPackages.isEmpty())
            m.exportedPackages = exportedPackages;
    }

    private static void initExportsAndOpens(Module m,
                                            Map<String, Module> nameToSource,
                                            Map<String, Module> nameToModule,
                                            List<ModuleLayer> parents) {
        ModuleDescriptor descriptor = m.getDescriptor();
        Map<String, Set<Module>> openPackages = new HashMap<>();
        Map<String, Set<Module>> exportedPackages = new HashMap<>();

        for (Opens opens : descriptor.opens()) {
            String source = opens.source();

            if (opens.isQualified()) {

                Set<Module> targets = new HashSet<>();
                for (String target : opens.targets()) {
                    Module m2 = findModule(target, nameToSource, nameToModule, parents);
                    if (m2 != null) {
                        addExports0(m, source, m2);
                        targets.add(m2);
                    }
                }
                if (!targets.isEmpty()) {
                    openPackages.put(source, targets);
                }
            } else {

                addExportsToAll0(m, source);
                openPackages.put(source, EVERYONE_SET);
            }
        }

        for (Exports exports : descriptor.exports()) {
            String source = exports.source();

            Set<Module> openToTargets = openPackages.get(source);
            if (openToTargets != null && openToTargets.contains(EVERYONE_MODULE))
                continue;

            if (exports.isQualified()) {

                Set<Module> targets = new HashSet<>();
                for (String target : exports.targets()) {
                    Module m2 = findModule(target, nameToSource, nameToModule, parents);
                    if (m2 != null) {

                        if (openToTargets == null || !openToTargets.contains(m2)) {
                            addExports0(m, source, m2);
                            targets.add(m2);
                        }
                    }
                }
                if (!targets.isEmpty()) {
                    exportedPackages.put(source, targets);
                }
            } else {

                addExportsToAll0(m, source);
                exportedPackages.put(source, EVERYONE_SET);
            }
        }

        if (!openPackages.isEmpty())
            m.openPackages = openPackages;
        if (!exportedPackages.isEmpty())
            m.exportedPackages = exportedPackages;
    }

    private static Module findModule(String target,
                                     Map<String, Module> nameToSource,
                                     Map<String, Module> nameToModule,
                                     List<ModuleLayer> parents) {
        Module m = nameToSource.get(target);
        if (m == null) {
            m = nameToModule.get(target);
            if (m == null) {
                for (ModuleLayer parent : parents) {
                    m = parent.findModule(target).orElse(null);
                    if (m != null) break;
                }
            }
        }
        return m;
    }

    @Override
    public <T extends Annotation> T getAnnotation(Class<T> annotationClass) {
        return moduleInfoClass().getDeclaredAnnotation(annotationClass);
    }

    @Override
    public Annotation[] getAnnotations() {
        return moduleInfoClass().getAnnotations();
    }

    @Override
    public Annotation[] getDeclaredAnnotations() {
        return moduleInfoClass().getDeclaredAnnotations();
    }

    private volatile Class<?> moduleInfoClass;

    private Class<?> moduleInfoClass() {
        Class<?> clazz = this.moduleInfoClass;
        if (clazz != null)
            return clazz;

        synchronized (this) {
            clazz = this.moduleInfoClass;
            if (clazz == null) {
                if (isNamed()) {
                    PrivilegedAction<Class<?>> pa = this::loadModuleInfoClass;
                    clazz = AccessController.doPrivileged(pa);
                }
                if (clazz == null) {
                    class DummyModuleInfo { }
                    clazz = DummyModuleInfo.class;
                }
                this.moduleInfoClass = clazz;
            }
            return clazz;
        }
    }

    private Class<?> loadModuleInfoClass() {
        Class<?> clazz = null;
        try (InputStream in = getResourceAsStream("module-info.class")) {
            if (in != null)
                clazz = loadModuleInfoClass(in);
        } catch (Exception ignore) { }
        return clazz;
    }

    private Class<?> loadModuleInfoClass(InputStream in) throws IOException {
        final String MODULE_INFO = "module-info";

        ClassWriter cw = new ClassWriter(ClassWriter.COMPUTE_MAXS
                                         + ClassWriter.COMPUTE_FRAMES);

        ClassVisitor cv = new ClassVisitor(Opcodes.ASM6, cw) {
            @Override
            public void visit(int version,
                              int access,
                              String name,
                              String signature,
                              String superName,
                              String[] interfaces) {
                cw.visit(version,
                        Opcodes.ACC_INTERFACE
                            + Opcodes.ACC_ABSTRACT
                            + Opcodes.ACC_SYNTHETIC,
                        MODULE_INFO,
                        null,
                        "java/lang/Object",
                        null);
            }
            @Override
            public AnnotationVisitor visitAnnotation(String desc, boolean visible) {

                return super.visitAnnotation(desc, visible);
            }
            @Override
            public void visitAttribute(Attribute attr) {

            }
            @Override
            public ModuleVisitor visitModule(String name, int flags, String version) {

                return null;
            }
        };

        ClassReader cr = new ClassReader(in);
        cr.accept(cv, 0);
        byte[] bytes = cw.toByteArray();

        ClassLoader cl = new ClassLoader(loader) {
            @Override
            protected Class<?> findClass(String cn)throws ClassNotFoundException {
                if (cn.equals(MODULE_INFO)) {
                    return super.defineClass(cn, bytes, 0, bytes.length);
                } else {
                    throw new ClassNotFoundException(cn);
                }
            }
            @Override
            protected Class<?> loadClass(String cn, boolean resolve)
                throws ClassNotFoundException
            {
                synchronized (getClassLoadingLock(cn)) {
                    Class<?> c = findLoadedClass(cn);
                    if (c == null) {
                        if (cn.equals(MODULE_INFO)) {
                            c = findClass(cn);
                        } else {
                            c = super.loadClass(cn, resolve);
                        }
                    }
                    if (resolve)
                        resolveClass(c);
                    return c;
                }
            }
        };

        try {
            return cl.loadClass(MODULE_INFO);
        } catch (ClassNotFoundException e) {
            throw new InternalError(e);
        }
    }

    @CallerSensitive
    public InputStream getResourceAsStream(String name) throws IOException {
        if (name.startsWith("/")) {
            name = name.substring(1);
        }

        if (isNamed() && Resources.canEncapsulate(name)) {
            Module caller = getCallerModule(Reflection.getCallerClass());
            if (caller != this && caller != Object.class.getModule()) {
                String pn = Resources.toPackageName(name);
                if (getPackages().contains(pn)) {
                    if (caller == null && !isOpen(pn)) {

                        return null;
                    }
                    if (!isOpen(pn, caller)) {

                        return null;
                    }
                }
            }
        }

        String mn = this.name;

        if (loader == null) {
            return BootLoader.findResourceAsStream(mn, name);
        } else if (loader instanceof BuiltinClassLoader) {
            return ((BuiltinClassLoader) loader).findResourceAsStream(mn, name);
        }

        URL url = loader.findResource(mn, name);
        if (url != null) {
            try {
                return url.openStream();
            } catch (SecurityException e) { }
        }

        return null;
    }

    @Override
    public String toString() {
        if (isNamed()) {
            return "module " + name;
        } else {
            String id = Integer.toHexString(System.identityHashCode(this));
            return "unnamed module @" + id;
        }
    }

    private Module getCallerModule(Class<?> caller) {
        return (caller != null) ? caller.getModule() : null;
    }

    private static native void defineModule0(Module module,
                                             boolean isOpen,
                                             String version,
                                             String location,
                                             String[] pns);

    private static native void addReads0(Module from, Module to);

    private static native void addExports0(Module from, String pn, Module to);

    private static native void addExportsToAll0(Module from, String pn);

    private static native void addExportsToAllUnnamed0(Module from, String pn);
}
