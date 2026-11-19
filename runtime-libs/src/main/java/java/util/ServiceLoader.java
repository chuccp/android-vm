package java.util;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.net.URL;
import java.net.URLConnection;
import java.security.AccessControlContext;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.security.PrivilegedActionException;
import java.security.PrivilegedExceptionAction;
import java.util.function.Consumer;
import java.util.function.Supplier;
import java.util.stream.Stream;
import java.util.stream.StreamSupport;

import sun.nio.cs.UTF_8;

import jdk.internal.loader.BootLoader;
import jdk.internal.loader.ClassLoaders;
import jdk.internal.misc.JavaLangAccess;
import jdk.internal.misc.SharedSecrets;
import jdk.internal.misc.VM;
import jdk.internal.module.ServicesCatalog;
import jdk.internal.module.ServicesCatalog.ServiceProvider;
import jdk.internal.reflect.CallerSensitive;
import jdk.internal.reflect.Reflection;

public final class ServiceLoader<S>
    implements Iterable<S>
{

    private final Class<S> service;

    private final String serviceName;

    private final ModuleLayer layer;

    private final ClassLoader loader;

    private final AccessControlContext acc;

    private Iterator<Provider<S>> lookupIterator1;
    private final List<S> instantiatedProviders = new ArrayList<>();

    private Iterator<Provider<S>> lookupIterator2;
    private final List<Provider<S>> loadedProviders = new ArrayList<>();
    private boolean loadedAllProviders;

    private int reloadCount;

    private static JavaLangAccess LANG_ACCESS;
    static {
        LANG_ACCESS = SharedSecrets.getJavaLangAccess();
    }

    public static interface Provider<S> extends Supplier<S> {

        Class<? extends S> type();

        @Override S get();
    }

    private ServiceLoader(Class<?> caller, ModuleLayer layer, Class<S> svc) {
        Objects.requireNonNull(caller);
        Objects.requireNonNull(layer);
        Objects.requireNonNull(svc);
        checkCaller(caller, svc);

        this.service = svc;
        this.serviceName = svc.getName();
        this.layer = layer;
        this.loader = null;
        this.acc = (System.getSecurityManager() != null)
                ? AccessController.getContext()
                : null;
    }

    private ServiceLoader(Class<?> caller, Class<S> svc, ClassLoader cl) {
        Objects.requireNonNull(svc);

        if (VM.isBooted()) {
            checkCaller(caller, svc);
            if (cl == null) {
                cl = ClassLoader.getSystemClassLoader();
            }
        } else {

            Module callerModule = caller.getModule();
            Module base = Object.class.getModule();
            Module svcModule = svc.getModule();
            if (callerModule != base || svcModule != base) {
                fail(svc, "not accessible to " + callerModule + " during VM init");
            }

            cl = null;
        }

        this.service = svc;
        this.serviceName = svc.getName();
        this.layer = null;
        this.loader = cl;
        this.acc = (System.getSecurityManager() != null)
                ? AccessController.getContext()
                : null;
    }

    private ServiceLoader(Module callerModule, Class<S> svc, ClassLoader cl) {
        if (!callerModule.canUse(svc)) {
            fail(svc, callerModule + " does not declare `uses`");
        }

        this.service = Objects.requireNonNull(svc);
        this.serviceName = svc.getName();
        this.layer = null;
        this.loader = cl;
        this.acc = (System.getSecurityManager() != null)
                ? AccessController.getContext()
                : null;
    }

    private static void checkCaller(Class<?> caller, Class<?> svc) {
        if (caller == null) {
            fail(svc, "no caller to check if it declares `uses`");
        }

        Module callerModule = caller.getModule();
        int mods = svc.getModifiers();
        if (!Reflection.verifyMemberAccess(caller, svc, null, mods)) {
            fail(svc, "service type not accessible to " + callerModule);
        }

        if (!callerModule.canUse(svc)) {
            fail(svc, callerModule + " does not declare `uses`");
        }
    }

    private static void fail(Class<?> service, String msg, Throwable cause)
        throws ServiceConfigurationError
    {
        throw new ServiceConfigurationError(service.getName() + ": " + msg,
                                            cause);
    }

    private static void fail(Class<?> service, String msg)
        throws ServiceConfigurationError
    {
        throw new ServiceConfigurationError(service.getName() + ": " + msg);
    }

    private static void fail(Class<?> service, URL u, int line, String msg)
        throws ServiceConfigurationError
    {
        fail(service, u + ":" + line + ": " + msg);
    }

    private boolean inExplicitModule(Class<?> clazz) {
        Module module = clazz.getModule();
        return module.isNamed() && !module.getDescriptor().isAutomatic();
    }

    private Method findStaticProviderMethod(Class<?> clazz) {
        List<Method> methods = null;
        try {
            methods = LANG_ACCESS.getDeclaredPublicMethods(clazz, "provider");
        } catch (Throwable x) {
            fail(service, "Unable to get public provider() method", x);
        }
        if (methods.isEmpty()) {

            return null;
        }

        Method result = null;
        for (Method method : methods) {
            int mods = method.getModifiers();
            assert Modifier.isPublic(mods);
            if (Modifier.isStatic(mods)) {
                if (result != null) {
                    fail(service, clazz + " declares more than one"
                         + " public static provider() method");
                }
                result = method;
            }
        }
        if (result != null) {
            Method m = result;
            PrivilegedAction<Void> pa = () -> {
                m.setAccessible(true);
                return null;
            };
            AccessController.doPrivileged(pa);
        }
        return result;
    }

    private Constructor<?> getConstructor(Class<?> clazz) {
        PrivilegedExceptionAction<Constructor<?>> pa
            = new PrivilegedExceptionAction<>() {
                @Override
                public Constructor<?> run() throws Exception {
                    Constructor<?> ctor = clazz.getConstructor();
                    if (inExplicitModule(clazz))
                        ctor.setAccessible(true);
                    return ctor;
                }
            };
        Constructor<?> ctor = null;
        try {
            ctor = AccessController.doPrivileged(pa);
        } catch (Throwable x) {
            if (x instanceof PrivilegedActionException)
                x = x.getCause();
            String cn = clazz.getName();
            fail(service, cn + " Unable to get public no-arg constructor", x);
        }
        return ctor;
    }

    private static class ProviderImpl<S> implements Provider<S> {
        final Class<S> service;
        final Class<? extends S> type;
        final Method factoryMethod;
        final Constructor<? extends S> ctor;
        final AccessControlContext acc;

        ProviderImpl(Class<S> service,
                     Class<? extends S> type,
                     Method factoryMethod,
                     AccessControlContext acc) {
            this.service = service;
            this.type = type;
            this.factoryMethod = factoryMethod;
            this.ctor = null;
            this.acc = acc;
        }

        ProviderImpl(Class<S> service,
                     Class<? extends S> type,
                     Constructor<? extends S> ctor,
                     AccessControlContext acc) {
            this.service = service;
            this.type = type;
            this.factoryMethod = null;
            this.ctor = ctor;
            this.acc = acc;
        }

        @Override
        public Class<? extends S> type() {
            return type;
        }

        @Override
        public S get() {
            if (factoryMethod != null) {
                return invokeFactoryMethod();
            } else {
                return newInstance();
            }
        }

        private S invokeFactoryMethod() {
            Object result = null;
            Throwable exc = null;
            if (acc == null) {
                try {
                    result = factoryMethod.invoke(null);
                } catch (Throwable x) {
                    exc = x;
                }
            } else {
                PrivilegedExceptionAction<?> pa = new PrivilegedExceptionAction<>() {
                    @Override
                    public Object run() throws Exception {
                        return factoryMethod.invoke(null);
                    }
                };

                try {
                    result = AccessController.doPrivileged(pa, acc);
                } catch (Throwable x) {
                    if (x instanceof PrivilegedActionException)
                        x = x.getCause();
                    exc = x;
                }
            }
            if (exc != null) {
                if (exc instanceof InvocationTargetException)
                    exc = exc.getCause();
                fail(service, factoryMethod + " failed", exc);
            }
            if (result == null) {
                fail(service, factoryMethod + " returned null");
            }
            @SuppressWarnings("unchecked")
            S p = (S) result;
            return p;
        }

        private S newInstance() {
            S p = null;
            Throwable exc = null;
            if (acc == null) {
                try {
                    p = ctor.newInstance();
                } catch (Throwable x) {
                    exc = x;
                }
            } else {
                PrivilegedExceptionAction<S> pa = new PrivilegedExceptionAction<>() {
                    @Override
                    public S run() throws Exception {
                        return ctor.newInstance();
                    }
                };

                try {
                    p = AccessController.doPrivileged(pa, acc);
                } catch (Throwable x) {
                    if (x instanceof PrivilegedActionException)
                        x = x.getCause();
                    exc = x;
                }
            }
            if (exc != null) {
                if (exc instanceof InvocationTargetException)
                    exc = exc.getCause();
                String cn = ctor.getDeclaringClass().getName();
                fail(service,
                     "Provider " + cn + " could not be instantiated", exc);
            }
            return p;
        }

        @Override
        public int hashCode() {
            return Objects.hash(service, type, acc);
        }

        @Override
        public boolean equals(Object ob) {
            if (!(ob instanceof ProviderImpl))
                return false;
            @SuppressWarnings("unchecked")
            ProviderImpl<?> that = (ProviderImpl<?>)ob;
            return this.service == that.service
                    && this.type == that.type
                    && Objects.equals(this.acc, that.acc);
        }
    }

    private Provider<S> loadProvider(ServiceProvider provider) {
        Module module = provider.module();
        if (!module.canRead(service.getModule())) {

            return null;
        }

        String cn = provider.providerName();
        Class<?> clazz = null;
        if (acc == null) {
            try {
                clazz = Class.forName(module, cn);
            } catch (LinkageError e) {
                fail(service, "Unable to load " + cn, e);
            }
        } else {
            PrivilegedExceptionAction<Class<?>> pa = () -> Class.forName(module, cn);
            try {
                clazz = AccessController.doPrivileged(pa);
            } catch (Throwable x) {
                if (x instanceof PrivilegedActionException)
                    x = x.getCause();
                fail(service, "Unable to load " + cn, x);
                return null;
            }
        }
        if (clazz == null) {
            fail(service, "Provider " + cn + " not found");
        }

        int mods = clazz.getModifiers();
        if (!Modifier.isPublic(mods)) {
            fail(service, clazz + " is not public");
        }

        if (inExplicitModule(clazz)) {
            Method factoryMethod = findStaticProviderMethod(clazz);
            if (factoryMethod != null) {
                Class<?> returnType = factoryMethod.getReturnType();
                if (!service.isAssignableFrom(returnType)) {
                    fail(service, factoryMethod + " return type not a subtype");
                }

                @SuppressWarnings("unchecked")
                Class<? extends S> type = (Class<? extends S>) returnType;
                return new ProviderImpl<S>(service, type, factoryMethod, acc);
            }
        }

        if (!service.isAssignableFrom(clazz)) {
            fail(service, clazz.getName() + " not a subtype");
        }

        @SuppressWarnings("unchecked")
        Class<? extends S> type = (Class<? extends S>) clazz;
        @SuppressWarnings("unchecked")
        Constructor<? extends S> ctor = (Constructor<? extends S> ) getConstructor(clazz);
        return new ProviderImpl<S>(service, type, ctor, acc);
    }

    private final class LayerLookupIterator<T>
        implements Iterator<Provider<T>>
    {
        Deque<ModuleLayer> stack = new ArrayDeque<>();
        Set<ModuleLayer> visited = new HashSet<>();
        Iterator<ServiceProvider> iterator;

        Provider<T> nextProvider;
        ServiceConfigurationError nextError;

        LayerLookupIterator() {
            visited.add(layer);
            stack.push(layer);
        }

        private Iterator<ServiceProvider> providers(ModuleLayer layer) {
            ServicesCatalog catalog = LANG_ACCESS.getServicesCatalog(layer);
            return catalog.findServices(serviceName).iterator();
        }

        @Override
        public boolean hasNext() {
            while (nextProvider == null && nextError == null) {

                while (iterator == null || !iterator.hasNext()) {

                    if (stack.isEmpty())
                        return false;

                    ModuleLayer layer = stack.pop();
                    List<ModuleLayer> parents = layer.parents();
                    for (int i = parents.size() - 1; i >= 0; i--) {
                        ModuleLayer parent = parents.get(i);
                        if (!visited.contains(parent)) {
                            visited.add(parent);
                            stack.push(parent);
                        }
                    }
                    iterator = providers(layer);
                }

                ServiceProvider provider = iterator.next();
                try {
                    @SuppressWarnings("unchecked")
                    Provider<T> next = (Provider<T>) loadProvider(provider);
                    nextProvider = next;
                } catch (ServiceConfigurationError e) {
                    nextError = e;
                }
            }
            return true;
        }

        @Override
        public Provider<T> next() {
            if (!hasNext())
                throw new NoSuchElementException();

            Provider<T> provider = nextProvider;
            if (provider != null) {
                nextProvider = null;
                return provider;
            } else {
                ServiceConfigurationError e = nextError;
                assert e != null;
                nextError = null;
                throw e;
            }
        }
    }

    private final class ModuleServicesLookupIterator<T>
        implements Iterator<Provider<T>>
    {
        ClassLoader currentLoader;
        Iterator<ServiceProvider> iterator;

        Provider<T> nextProvider;
        ServiceConfigurationError nextError;

        ModuleServicesLookupIterator() {
            this.currentLoader = loader;
            this.iterator = iteratorFor(loader);
        }

        private List<ServiceProvider> providers(ModuleLayer layer) {
            ServicesCatalog catalog = LANG_ACCESS.getServicesCatalog(layer);
            return catalog.findServices(serviceName);
        }

        private ClassLoader loaderFor(Module module) {
            SecurityManager sm = System.getSecurityManager();
            if (sm == null) {
                return module.getClassLoader();
            } else {
                PrivilegedAction<ClassLoader> pa = module::getClassLoader;
                return AccessController.doPrivileged(pa);
            }
        }

        private Iterator<ServiceProvider> iteratorFor(ClassLoader loader) {

            ServicesCatalog catalog;
            if (loader == null) {
                catalog = BootLoader.getServicesCatalog();
            } else {
                catalog = ServicesCatalog.getServicesCatalogOrNull(loader);
            }
            List<ServiceProvider> providers;
            if (catalog == null) {
                providers = List.of();
            } else {
                providers = catalog.findServices(serviceName);
            }

            ClassLoader platformClassLoader = ClassLoaders.platformClassLoader();
            if (loader == null || loader == platformClassLoader) {
                return providers.iterator();
            } else {
                List<ServiceProvider> allProviders = new ArrayList<>(providers);
                Iterator<ModuleLayer> iterator = LANG_ACCESS.layers(loader).iterator();
                while (iterator.hasNext()) {
                    ModuleLayer layer = iterator.next();
                    for (ServiceProvider sp : providers(layer)) {
                        ClassLoader l = loaderFor(sp.module());
                        if (l != null && l != platformClassLoader) {
                            allProviders.add(sp);
                        }
                    }
                }
                return allProviders.iterator();
            }
        }

        @Override
        public boolean hasNext() {
            while (nextProvider == null && nextError == null) {

                while (!iterator.hasNext()) {
                    if (currentLoader == null) {
                        return false;
                    } else {
                        currentLoader = currentLoader.getParent();
                        iterator = iteratorFor(currentLoader);
                    }
                }

                ServiceProvider provider = iterator.next();
                try {
                    @SuppressWarnings("unchecked")
                    Provider<T> next = (Provider<T>) loadProvider(provider);
                    nextProvider = next;
                } catch (ServiceConfigurationError e) {
                    nextError = e;
                }
            }
            return true;
        }

        @Override
        public Provider<T> next() {
            if (!hasNext())
                throw new NoSuchElementException();

            Provider<T> provider = nextProvider;
            if (provider != null) {
                nextProvider = null;
                return provider;
            } else {
                ServiceConfigurationError e = nextError;
                assert e != null;
                nextError = null;
                throw e;
            }
        }
    }

    private final class LazyClassPathLookupIterator<T>
        implements Iterator<Provider<T>>
    {
        static final String PREFIX = "META-INF/services/";

        Set<String> providerNames = new HashSet<>();
        Enumeration<URL> configs;
        Iterator<String> pending;

        Provider<T> nextProvider;
        ServiceConfigurationError nextError;

        LazyClassPathLookupIterator() { }

        private int parseLine(URL u, BufferedReader r, int lc, Set<String> names)
            throws IOException
        {
            String ln = r.readLine();
            if (ln == null) {
                return -1;
            }
            int ci = ln.indexOf('#');
            if (ci >= 0) ln = ln.substring(0, ci);
            ln = ln.trim();
            int n = ln.length();
            if (n != 0) {
                if ((ln.indexOf(' ') >= 0) || (ln.indexOf('\t') >= 0))
                    fail(service, u, lc, "Illegal configuration-file syntax");
                int cp = ln.codePointAt(0);
                if (!Character.isJavaIdentifierStart(cp))
                    fail(service, u, lc, "Illegal provider-class name: " + ln);
                int start = Character.charCount(cp);
                for (int i = start; i < n; i += Character.charCount(cp)) {
                    cp = ln.codePointAt(i);
                    if (!Character.isJavaIdentifierPart(cp) && (cp != '.'))
                        fail(service, u, lc, "Illegal provider-class name: " + ln);
                }
                if (providerNames.add(ln)) {
                    names.add(ln);
                }
            }
            return lc + 1;
        }

        private Iterator<String> parse(URL u) {
            Set<String> names = new LinkedHashSet<>();
            try {
                URLConnection uc = u.openConnection();
                uc.setUseCaches(false);
                try (InputStream in = uc.getInputStream();
                     BufferedReader r
                         = new BufferedReader(new InputStreamReader(in, UTF_8.INSTANCE)))
                {
                    int lc = 1;
                    while ((lc = parseLine(u, r, lc, names)) >= 0);
                }
            } catch (IOException x) {
                fail(service, "Error accessing configuration file", x);
            }
            return names.iterator();
        }

        private Class<?> nextProviderClass() {
            if (configs == null) {
                try {
                    String fullName = PREFIX + service.getName();
                    if (loader == null) {
                        configs = ClassLoader.getSystemResources(fullName);
                    } else if (loader == ClassLoaders.platformClassLoader()) {

                        if (BootLoader.hasClassPath()) {
                            configs = BootLoader.findResources(fullName);
                        } else {
                            configs = Collections.emptyEnumeration();
                        }
                    } else {
                        configs = loader.getResources(fullName);
                    }
                } catch (IOException x) {
                    fail(service, "Error locating configuration files", x);
                }
            }
            while ((pending == null) || !pending.hasNext()) {
                if (!configs.hasMoreElements()) {
                    return null;
                }
                pending = parse(configs.nextElement());
            }
            String cn = pending.next();
            try {
                return Class.forName(cn, false, loader);
            } catch (ClassNotFoundException x) {
                fail(service, "Provider " + cn + " not found");
                return null;
            }
        }

        @SuppressWarnings("unchecked")
        private boolean hasNextService() {
            while (nextProvider == null && nextError == null) {
                try {
                    Class<?> clazz = nextProviderClass();
                    if (clazz == null)
                        return false;

                    if (clazz.getModule().isNamed()) {

                        continue;
                    }

                    if (service.isAssignableFrom(clazz)) {
                        Class<? extends S> type = (Class<? extends S>) clazz;
                        Constructor<? extends S> ctor
                            = (Constructor<? extends S>)getConstructor(clazz);
                        ProviderImpl<S> p = new ProviderImpl<S>(service, type, ctor, acc);
                        nextProvider = (ProviderImpl<T>) p;
                    } else {
                        fail(service, clazz.getName() + " not a subtype");
                    }
                } catch (ServiceConfigurationError e) {
                    nextError = e;
                }
            }
            return true;
        }

        private Provider<T> nextService() {
            if (!hasNextService())
                throw new NoSuchElementException();

            Provider<T> provider = nextProvider;
            if (provider != null) {
                nextProvider = null;
                return provider;
            } else {
                ServiceConfigurationError e = nextError;
                assert e != null;
                nextError = null;
                throw e;
            }
        }

        @Override
        public boolean hasNext() {
            if (acc == null) {
                return hasNextService();
            } else {
                PrivilegedAction<Boolean> action = new PrivilegedAction<>() {
                    public Boolean run() { return hasNextService(); }
                };
                return AccessController.doPrivileged(action, acc);
            }
        }

        @Override
        public Provider<T> next() {
            if (acc == null) {
                return nextService();
            } else {
                PrivilegedAction<Provider<T>> action = new PrivilegedAction<>() {
                    public Provider<T> run() { return nextService(); }
                };
                return AccessController.doPrivileged(action, acc);
            }
        }
    }

    private Iterator<Provider<S>> newLookupIterator() {
        assert layer == null || loader == null;
        if (layer != null) {
            return new LayerLookupIterator<>();
        } else {
            Iterator<Provider<S>> first = new ModuleServicesLookupIterator<>();
            Iterator<Provider<S>> second = new LazyClassPathLookupIterator<>();
            return new Iterator<Provider<S>>() {
                @Override
                public boolean hasNext() {
                    return (first.hasNext() || second.hasNext());
                }
                @Override
                public Provider<S> next() {
                    if (first.hasNext()) {
                        return first.next();
                    } else if (second.hasNext()) {
                        return second.next();
                    } else {
                        throw new NoSuchElementException();
                    }
                }
            };
        }
    }

    // VM 兼容偏差（宿主环境与 HotSpot 的差异点）：
    // 单个 provider 构造失败（ServiceConfigurationError）时跳过并继续，
    // 不中止整条服务发现链。加固壳/厂商 SDK 的个别 provider 构造器在
    // 非 ART 环境下会抛异常（如依赖壳解密结果），旧行为会让同一接口的
    // 其余 provider 全部丢失（实测 BuilderDecorator 链整体为空）。
    // hasNext() 预取（含实例化）以保证失败 provider 不会以
    // hasNext=true/next 抛错的形式泄漏给调用方。
    private S nextHealthyProvider() {
        while (true) {
            if (!lookupIterator1.hasNext()) return null;
            Provider<S> p = lookupIterator1.next();
            S value;
            try {
                value = p.get();
            } catch (ServiceConfigurationError e) {
                continue;  // 跳过不可实例化 provider
            }
            instantiatedProviders.add(value);
            return value;
        }
    }

    public Iterator<S> iterator() {

        if (lookupIterator1 == null) {
            lookupIterator1 = newLookupIterator();
        }

        return new Iterator<S>() {

            final int expectedReloadCount = ServiceLoader.this.reloadCount;

            int index;
            S pending;
            boolean pendingReady;

            private void checkReloadCount() {
                if (ServiceLoader.this.reloadCount != expectedReloadCount)
                    throw new ConcurrentModificationException();
            }

            private boolean fill() {
                if (pendingReady) return true;
                if (index < instantiatedProviders.size()) {
                    pending = instantiatedProviders.get(index);
                    pendingReady = true;
                    return true;
                }
                pending = nextHealthyProvider();
                pendingReady = pending != null;
                return pendingReady;
            }

            @Override
            public boolean hasNext() {
                checkReloadCount();
                return fill();
            }

            @Override
            public S next() {
                checkReloadCount();
                if (!fill()) throw new NoSuchElementException();
                S next = pending;
                pending = null;
                pendingReady = false;
                index++;
                return next;
            }

        };
    }

    public Stream<Provider<S>> stream() {

        if (loadedAllProviders) {
            return loadedProviders.stream();
        }

        if (lookupIterator2 == null) {
            lookupIterator2 = newLookupIterator();
        }

        Spliterator<Provider<S>> s = new ProviderSpliterator<>(lookupIterator2);
        return StreamSupport.stream(s, false);
    }

    private class ProviderSpliterator<T> implements Spliterator<Provider<T>> {
        final int expectedReloadCount = ServiceLoader.this.reloadCount;
        final Iterator<Provider<T>> iterator;
        int index;

        ProviderSpliterator(Iterator<Provider<T>> iterator) {
            this.iterator = iterator;
        }

        @Override
        public Spliterator<Provider<T>> trySplit() {
            return null;
        }

        @Override
        @SuppressWarnings("unchecked")
        public boolean tryAdvance(Consumer<? super Provider<T>> action) {
            if (ServiceLoader.this.reloadCount != expectedReloadCount)
                throw new ConcurrentModificationException();
            Provider<T> next = null;
            if (index < loadedProviders.size()) {
                next = (Provider<T>) loadedProviders.get(index++);
            } else if (iterator.hasNext()) {
                next = iterator.next();
                loadedProviders.add((Provider<S>)next);
                index++;
            } else {
                loadedAllProviders = true;
            }
            if (next != null) {
                action.accept(next);
                return true;
            } else {
                return false;
            }
        }

        @Override
        public int characteristics() {

            return Spliterator.ORDERED;
        }

        @Override
        public long estimateSize() {
            return Long.MAX_VALUE;
        }
    }

    static <S> ServiceLoader<S> load(Class<S> service,
                                     ClassLoader loader,
                                     Module callerModule)
    {
        return new ServiceLoader<>(callerModule, service, loader);
    }

    @CallerSensitive
    public static <S> ServiceLoader<S> load(Class<S> service,
                                            ClassLoader loader)
    {
        return new ServiceLoader<>(Reflection.getCallerClass(), service, loader);
    }

    @CallerSensitive
    public static <S> ServiceLoader<S> load(Class<S> service) {
        ClassLoader cl = Thread.currentThread().getContextClassLoader();
        return new ServiceLoader<>(Reflection.getCallerClass(), service, cl);
    }

    @CallerSensitive
    public static <S> ServiceLoader<S> loadInstalled(Class<S> service) {
        ClassLoader cl = ClassLoader.getPlatformClassLoader();
        return new ServiceLoader<>(Reflection.getCallerClass(), service, cl);
    }

    @CallerSensitive
    public static <S> ServiceLoader<S> load(ModuleLayer layer, Class<S> service) {
        return new ServiceLoader<>(Reflection.getCallerClass(), layer, service);
    }

    public Optional<S> findFirst() {
        Iterator<S> iterator = iterator();
        if (iterator.hasNext()) {
            return Optional.of(iterator.next());
        } else {
            return Optional.empty();
        }
    }

    public void reload() {
        lookupIterator1 = null;
        instantiatedProviders.clear();

        lookupIterator2 = null;
        loadedProviders.clear();
        loadedAllProviders = false;

        reloadCount++;
    }

    public String toString() {
        return "java.util.ServiceLoader[" + service.getName() + "]";
    }

}
