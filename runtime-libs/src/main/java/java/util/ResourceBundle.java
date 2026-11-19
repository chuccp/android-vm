package java.util;

import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.lang.ref.Reference;
import java.lang.ref.ReferenceQueue;
import java.lang.ref.SoftReference;
import java.lang.ref.WeakReference;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Modifier;
import java.net.JarURLConnection;
import java.net.URL;
import java.net.URLConnection;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.security.PrivilegedActionException;
import java.security.PrivilegedExceptionAction;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.jar.JarEntry;
import java.util.spi.ResourceBundleControlProvider;
import java.util.spi.ResourceBundleProvider;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import jdk.internal.loader.BootLoader;
import jdk.internal.misc.JavaUtilResourceBundleAccess;
import jdk.internal.misc.SharedSecrets;
import jdk.internal.reflect.CallerSensitive;
import jdk.internal.reflect.Reflection;
import sun.security.action.GetPropertyAction;
import sun.util.locale.BaseLocale;
import sun.util.locale.LocaleObjectCache;
import static sun.security.util.SecurityConstants.GET_CLASSLOADER_PERMISSION;

public abstract class ResourceBundle {

    private static final int INITIAL_CACHE_SIZE = 32;

    static {
        SharedSecrets.setJavaUtilResourceBundleAccess(
            new JavaUtilResourceBundleAccess() {
                @Override
                public void setParent(ResourceBundle bundle,
                                      ResourceBundle parent) {
                    bundle.setParent(parent);
                }

                @Override
                public ResourceBundle getParent(ResourceBundle bundle) {
                    return bundle.parent;
                }

                @Override
                public void setLocale(ResourceBundle bundle, Locale locale) {
                    bundle.locale = locale;
                }

                @Override
                public void setName(ResourceBundle bundle, String name) {
                    bundle.name = name;
                }

                @Override
                public ResourceBundle getBundle(String baseName, Locale locale, Module module) {

                    return getBundleImpl(module, module,
                                         baseName, locale,
                                         getDefaultControl(module, baseName));
                }

                @Override
                public ResourceBundle newResourceBundle(Class<? extends ResourceBundle> bundleClass) {
                    return ResourceBundleProviderHelper.newResourceBundle(bundleClass);
                }
            });
    }

    private static final ResourceBundle NONEXISTENT_BUNDLE = new ResourceBundle() {
            public Enumeration<String> getKeys() { return null; }
            protected Object handleGetObject(String key) { return null; }
            public String toString() { return "NONEXISTENT_BUNDLE"; }
        };

    private static final ConcurrentMap<CacheKey, BundleReference> cacheList
        = new ConcurrentHashMap<>(INITIAL_CACHE_SIZE);

    private static final ReferenceQueue<Object> referenceQueue = new ReferenceQueue<>();

    public String getBaseBundleName() {
        return name;
    }

    protected ResourceBundle parent = null;

    private Locale locale = null;

    private String name;

    private volatile boolean expired;

    private volatile CacheKey cacheKey;

    private volatile Set<String> keySet;

    public ResourceBundle() {
    }

    public final String getString(String key) {
        return (String) getObject(key);
    }

    public final String[] getStringArray(String key) {
        return (String[]) getObject(key);
    }

    public final Object getObject(String key) {
        Object obj = handleGetObject(key);
        if (obj == null) {
            if (parent != null) {
                obj = parent.getObject(key);
            }
            if (obj == null) {
                throw new MissingResourceException("Can't find resource for bundle "
                                                   +this.getClass().getName()
                                                   +", key "+key,
                                                   this.getClass().getName(),
                                                   key);
            }
        }
        return obj;
    }

    public Locale getLocale() {
        return locale;
    }

    private static ClassLoader getLoader(Module module) {
        PrivilegedAction<ClassLoader> pa = module::getClassLoader;
        return AccessController.doPrivileged(pa);
    }

    private static ClassLoader getLoaderForControl(Module module) {
        ClassLoader loader = getLoader(module);
        return loader == null ? ClassLoader.getPlatformClassLoader() : loader;
    }

    protected void setParent(ResourceBundle parent) {
        assert parent != NONEXISTENT_BUNDLE;
        this.parent = parent;
    }

    private static final class CacheKey {

        private final String name;
        private volatile Locale locale;
        private final KeyElementReference<Module> moduleRef;
        private final KeyElementReference<Module> callerRef;

        private final int modulesHash;

        private volatile String format;

        private volatile long loadTime;

        private volatile long expirationTime;

        private volatile Throwable cause;

        private volatile ServiceLoader<ResourceBundleProvider> providers;
        private volatile boolean providersChecked;

        private volatile Boolean callerHasProvider;

        CacheKey(String baseName, Locale locale, Module module, Module caller) {
            Objects.requireNonNull(module);
            Objects.requireNonNull(caller);

            this.name = baseName;
            this.locale = locale;
            this.moduleRef = new KeyElementReference<>(module, referenceQueue, this);
            this.callerRef = new KeyElementReference<>(caller, referenceQueue, this);
            this.modulesHash = module.hashCode() ^ caller.hashCode();
        }

        CacheKey(CacheKey src) {

            this.moduleRef = new KeyElementReference<>(
                Objects.requireNonNull(src.getModule()), referenceQueue, this);
            this.callerRef = new KeyElementReference<>(
                Objects.requireNonNull(src.getCallerModule()), referenceQueue, this);

            this.name = src.name;
            this.locale = src.locale;
            this.modulesHash = src.modulesHash;
            this.format = src.format;
            this.loadTime = src.loadTime;
            this.expirationTime = src.expirationTime;
        }

        String getName() {
            return name;
        }

        Locale getLocale() {
            return locale;
        }

        CacheKey setLocale(Locale locale) {
            this.locale = locale;
            return this;
        }

        Module getModule() {
            return moduleRef.get();
        }

        Module getCallerModule() {
            return callerRef.get();
        }

        ServiceLoader<ResourceBundleProvider> getProviders() {
            if (!providersChecked) {
                providers = getServiceLoader(getModule(), name);
                providersChecked = true;
            }
            return providers;
        }

        boolean hasProviders() {
            return getProviders() != null;
        }

        boolean callerHasProvider() {
            return callerHasProvider == Boolean.TRUE;
        }

        @Override
        public boolean equals(Object other) {
            if (this == other) {
                return true;
            }
            try {
                final CacheKey otherEntry = (CacheKey)other;

                if (modulesHash != otherEntry.modulesHash) {
                    return false;
                }

                if (!name.equals(otherEntry.name)) {
                    return false;
                }

                if (!locale.equals(otherEntry.locale)) {
                    return false;
                }

                Module module = getModule();
                Module caller = getCallerModule();
                return ((module != null) && (module.equals(otherEntry.getModule())) &&
                        (caller != null) && (caller.equals(otherEntry.getCallerModule())));
            } catch (NullPointerException | ClassCastException e) {
            }
            return false;
        }

        @Override
        public int hashCode() {
            return (name.hashCode() << 3) ^ locale.hashCode() ^ modulesHash;
        }

        String getFormat() {
            return format;
        }

        void setFormat(String format) {
            this.format = format;
        }

        private void setCause(Throwable cause) {
            if (this.cause == null) {
                this.cause = cause;
            } else {

                if (this.cause instanceof ClassNotFoundException) {
                    this.cause = cause;
                }
            }
        }

        private Throwable getCause() {
            return cause;
        }

        @Override
        public String toString() {
            String l = locale.toString();
            if (l.isEmpty()) {
                if (!locale.getVariant().isEmpty()) {
                    l = "__" + locale.getVariant();
                } else {
                    l = "\"\"";
                }
            }
            return "CacheKey[" + name +
                   ", locale=" + l +
                   ", module=" + getModule() +
                   ", callerModule=" + getCallerModule() +
                   ", format=" + format +
                   "]";
        }
    }

    private static interface CacheKeyReference {
        public CacheKey getCacheKey();
    }

    private static class KeyElementReference<T> extends WeakReference<T>
                                                implements CacheKeyReference {
        private final CacheKey cacheKey;

        KeyElementReference(T referent, ReferenceQueue<Object> q, CacheKey key) {
            super(referent, q);
            cacheKey = key;
        }

        @Override
        public CacheKey getCacheKey() {
            return cacheKey;
        }
    }

    private static class BundleReference extends SoftReference<ResourceBundle>
                                         implements CacheKeyReference {
        private final CacheKey cacheKey;

        BundleReference(ResourceBundle referent, ReferenceQueue<Object> q, CacheKey key) {
            super(referent, q);
            cacheKey = key;
        }

        @Override
        public CacheKey getCacheKey() {
            return cacheKey;
        }
    }

    @CallerSensitive
    public static final ResourceBundle getBundle(String baseName)
    {
        Class<?> caller = Reflection.getCallerClass();
        return getBundleImpl(baseName, Locale.getDefault(),
                             caller, getDefaultControl(caller, baseName));
    }

    @CallerSensitive
    public static final ResourceBundle getBundle(String baseName,
                                                 Control control) {
        Class<?> caller = Reflection.getCallerClass();
        Locale targetLocale = Locale.getDefault();
        checkNamedModule(caller);
        return getBundleImpl(baseName, targetLocale, caller, control);
    }

    @CallerSensitive
    public static final ResourceBundle getBundle(String baseName,
                                                 Locale locale)
    {
        Class<?> caller = Reflection.getCallerClass();
        return getBundleImpl(baseName, locale,
                             caller, getDefaultControl(caller, baseName));
    }

    @CallerSensitive
    public static ResourceBundle getBundle(String baseName, Module module) {
        return getBundleFromModule(Reflection.getCallerClass(), module, baseName,
                                   Locale.getDefault(),
                                   getDefaultControl(module, baseName));
    }

    @CallerSensitive
    public static ResourceBundle getBundle(String baseName, Locale targetLocale, Module module) {
        return getBundleFromModule(Reflection.getCallerClass(), module, baseName, targetLocale,
                                   getDefaultControl(module, baseName));
    }

    @CallerSensitive
    public static final ResourceBundle getBundle(String baseName, Locale targetLocale,
                                                 Control control) {
        Class<?> caller = Reflection.getCallerClass();
        checkNamedModule(caller);
        return getBundleImpl(baseName, targetLocale, caller, control);
    }

    @CallerSensitive
    public static ResourceBundle getBundle(String baseName, Locale locale,
                                           ClassLoader loader)
    {
        if (loader == null) {
            throw new NullPointerException();
        }
        Class<?> caller = Reflection.getCallerClass();
        return getBundleImpl(baseName, locale, caller, loader, getDefaultControl(caller, baseName));
    }

    @CallerSensitive
    public static ResourceBundle getBundle(String baseName, Locale targetLocale,
                                           ClassLoader loader, Control control) {
        if (loader == null || control == null) {
            throw new NullPointerException();
        }
        Class<?> caller = Reflection.getCallerClass();
        checkNamedModule(caller);
        return getBundleImpl(baseName, targetLocale, caller, loader, control);
    }

    private static Control getDefaultControl(Class<?> caller, String baseName) {
        return getDefaultControl(caller.getModule(), baseName);
    }

    private static Control getDefaultControl(Module targetModule, String baseName) {
        return targetModule.isNamed() ?
            Control.INSTANCE :
            ResourceBundleControlProviderHolder.getControl(baseName);
    }

    private static class ResourceBundleControlProviderHolder {
        private static final PrivilegedAction<List<ResourceBundleControlProvider>> pa =
            () -> {
                return Collections.unmodifiableList(
                    ServiceLoader.load(ResourceBundleControlProvider.class,
                                       ClassLoader.getSystemClassLoader()).stream()
                        .map(ServiceLoader.Provider::get)
                        .collect(Collectors.toList()));
            };

        private static final List<ResourceBundleControlProvider> CONTROL_PROVIDERS =
            AccessController.doPrivileged(pa);

        private static Control getControl(String baseName) {
            return CONTROL_PROVIDERS.isEmpty() ?
                Control.INSTANCE :
                CONTROL_PROVIDERS.stream()
                    .flatMap(provider -> Stream.ofNullable(provider.getControl(baseName)))
                    .findFirst()
                    .orElse(Control.INSTANCE);
        }
    }

    private static void checkNamedModule(Class<?> caller) {
        if (caller.getModule().isNamed()) {
            throw new UnsupportedOperationException(
                    "ResourceBundle.Control not supported in named modules");
        }
    }

    private static ResourceBundle getBundleImpl(String baseName,
                                                Locale locale,
                                                Class<?> caller,
                                                Control control) {
        return getBundleImpl(baseName, locale, caller, caller.getClassLoader(), control);
    }

    private static ResourceBundle getBundleImpl(String baseName,
                                                Locale locale,
                                                Class<?> caller,
                                                ClassLoader loader,
                                                Control control) {
        if (caller == null) {
            throw new InternalError("null caller");
        }
        Module callerModule = caller.getModule();

        if (callerModule.isNamed() && loader == getLoader(callerModule)) {
            return getBundleImpl(callerModule, callerModule, baseName, locale, control);
        }

        Module unnamedModule = loader != null
            ? loader.getUnnamedModule()
            : BootLoader.getUnnamedModule();

        return getBundleImpl(callerModule, unnamedModule, baseName, locale, control);
    }

    private static ResourceBundle getBundleFromModule(Class<?> caller,
                                                      Module module,
                                                      String baseName,
                                                      Locale locale,
                                                      Control control) {
        Objects.requireNonNull(module);
        Module callerModule = caller.getModule();
        if (callerModule != module) {
            SecurityManager sm = System.getSecurityManager();
            if (sm != null) {
                sm.checkPermission(GET_CLASSLOADER_PERMISSION);
            }
        }
        return getBundleImpl(callerModule, module, baseName, locale, control);
    }

    private static ResourceBundle getBundleImpl(Module callerModule,
                                                Module module,
                                                String baseName,
                                                Locale locale,
                                                Control control) {
        if (locale == null || control == null) {
            throw new NullPointerException();
        }

        CacheKey cacheKey = new CacheKey(baseName, locale, module, callerModule);
        ResourceBundle bundle = null;

        BundleReference bundleRef = cacheList.get(cacheKey);
        if (bundleRef != null) {
            bundle = bundleRef.get();
            bundleRef = null;
        }

        if (isValidBundle(bundle) && hasValidParentChain(bundle)) {
            return bundle;
        }

        boolean isKnownControl = (control == Control.INSTANCE) ||
                                   (control instanceof SingleFormatControl);
        List<String> formats = control.getFormats(baseName);
        if (!isKnownControl && !checkList(formats)) {
            throw new IllegalArgumentException("Invalid Control: getFormats");
        }

        ResourceBundle baseBundle = null;
        for (Locale targetLocale = locale;
             targetLocale != null;
             targetLocale = control.getFallbackLocale(baseName, targetLocale)) {
            List<Locale> candidateLocales = control.getCandidateLocales(baseName, targetLocale);
            if (!isKnownControl && !checkList(candidateLocales)) {
                throw new IllegalArgumentException("Invalid Control: getCandidateLocales");
            }

            bundle = findBundle(callerModule, module, cacheKey,
                                candidateLocales, formats, 0, control, baseBundle);

            if (isValidBundle(bundle)) {
                boolean isBaseBundle = Locale.ROOT.equals(bundle.locale);
                if (!isBaseBundle || bundle.locale.equals(locale)
                    || (candidateLocales.size() == 1
                        && bundle.locale.equals(candidateLocales.get(0)))) {
                    break;
                }

                if (isBaseBundle && baseBundle == null) {
                    baseBundle = bundle;
                }
            }
        }

        if (bundle == null) {
            if (baseBundle == null) {
                throwMissingResourceException(baseName, locale, cacheKey.getCause());
            }
            bundle = baseBundle;
        }

        Reference.reachabilityFence(callerModule);
        Reference.reachabilityFence(module);

        return bundle;
    }

    private static boolean checkList(List<?> a) {
        boolean valid = (a != null && !a.isEmpty());
        if (valid) {
            int size = a.size();
            for (int i = 0; valid && i < size; i++) {
                valid = (a.get(i) != null);
            }
        }
        return valid;
    }

    private static ResourceBundle findBundle(Module callerModule,
                                             Module module,
                                             CacheKey cacheKey,
                                             List<Locale> candidateLocales,
                                             List<String> formats,
                                             int index,
                                             Control control,
                                             ResourceBundle baseBundle) {
        Locale targetLocale = candidateLocales.get(index);
        ResourceBundle parent = null;
        if (index != candidateLocales.size() - 1) {
            parent = findBundle(callerModule, module, cacheKey,
                                candidateLocales, formats, index + 1,
                                control, baseBundle);
        } else if (baseBundle != null && Locale.ROOT.equals(targetLocale)) {
            return baseBundle;
        }

        Object ref;
        while ((ref = referenceQueue.poll()) != null) {
            cacheList.remove(((CacheKeyReference)ref).getCacheKey());
        }

        boolean expiredBundle = false;

        cacheKey.setLocale(targetLocale);
        ResourceBundle bundle = findBundleInCache(cacheKey, control);
        if (isValidBundle(bundle)) {
            expiredBundle = bundle.expired;
            if (!expiredBundle) {

                if (bundle.parent == parent) {
                    return bundle;
                }

                BundleReference bundleRef = cacheList.get(cacheKey);
                if (bundleRef != null && bundleRef.get() == bundle) {
                    cacheList.remove(cacheKey, bundleRef);
                }
            }
        }

        if (bundle != NONEXISTENT_BUNDLE) {
            trace("findBundle: %d %s %s formats: %s%n", index, candidateLocales, cacheKey, formats);
            if (module.isNamed()) {
                bundle = loadBundle(cacheKey, formats, control, module, callerModule);
            } else {
                bundle = loadBundle(cacheKey, formats, control, expiredBundle);
            }
            if (bundle != null) {
                if (bundle.parent == null) {
                    bundle.setParent(parent);
                }
                bundle.locale = targetLocale;
                bundle = putBundleInCache(cacheKey, bundle, control);
                return bundle;
            }

            putBundleInCache(cacheKey, NONEXISTENT_BUNDLE, control);
        }
        return parent;
    }

    private static final String UNKNOWN_FORMAT = "";

    private static ResourceBundle loadBundle(CacheKey cacheKey,
                                             List<String> formats,
                                             Control control,
                                             Module module,
                                             Module callerModule) {
        String baseName = cacheKey.getName();
        Locale targetLocale = cacheKey.getLocale();

        ResourceBundle bundle = null;
        if (cacheKey.hasProviders()) {
            if (callerModule == module) {
                bundle = loadBundleFromProviders(baseName,
                                                 targetLocale,
                                                 cacheKey.getProviders(),
                                                 cacheKey);
            } else {

                ClassLoader loader = getLoader(module);
                Class<ResourceBundleProvider> svc =
                    getResourceBundleProviderType(baseName, loader);
                if (svc != null
                        && Reflection.verifyModuleAccess(callerModule, svc)
                        && callerModule.canUse(svc)) {
                    bundle = loadBundleFromProviders(baseName,
                                                     targetLocale,
                                                     cacheKey.getProviders(),
                                                     cacheKey);
                }
            }

            if (bundle != null) {
                cacheKey.setFormat(UNKNOWN_FORMAT);
            }
        }

        if (bundle == null && !cacheKey.callerHasProvider()) {
            for (String format : formats) {
                try {
                    switch (format) {
                    case "java.class":
                        bundle = ResourceBundleProviderHelper
                            .loadResourceBundle(callerModule, module, baseName, targetLocale);

                        break;
                    case "java.properties":
                        bundle = ResourceBundleProviderHelper
                            .loadPropertyResourceBundle(callerModule, module, baseName, targetLocale);
                        break;
                    default:
                        throw new InternalError("unexpected format: " + format);
                    }

                    if (bundle != null) {
                        cacheKey.setFormat(format);
                        break;
                    }
                } catch (LinkageError|Exception e) {
                    cacheKey.setCause(e);
                }
            }
        }
        return bundle;
    }

    private static ServiceLoader<ResourceBundleProvider> getServiceLoader(Module module,
                                                                          String baseName)
    {
        if (!module.isNamed()) {
            return null;
        }

        ClassLoader loader = getLoader(module);
        Class<ResourceBundleProvider> service =
                getResourceBundleProviderType(baseName, loader);
        if (service != null && Reflection.verifyModuleAccess(module, service)) {
            try {

                return ServiceLoader.load(service, loader, module);
            } catch (ServiceConfigurationError e) {

                return null;
            }
        }
        return null;
    }

    private static Class<ResourceBundleProvider>
            getResourceBundleProviderType(String baseName, ClassLoader loader)
    {

        int i = baseName.lastIndexOf('.');
        if (i <= 0) {
            return null;
        }

        String name = baseName.substring(i+1, baseName.length()) + "Provider";
        String providerName = baseName.substring(0, i) + ".spi." + name;

        return AccessController.doPrivileged(
            new PrivilegedAction<>() {
                @Override
                public Class<ResourceBundleProvider> run() {
                    try {
                        Class<?> c = Class.forName(providerName, false, loader);
                        if (ResourceBundleProvider.class.isAssignableFrom(c)) {
                            @SuppressWarnings("unchecked")
                            Class<ResourceBundleProvider> s = (Class<ResourceBundleProvider>) c;
                            return s;
                        }
                    } catch (ClassNotFoundException e) {}
                    return null;
                }
            });
    }

    private static ResourceBundle loadBundleFromProviders(String baseName,
                                                          Locale locale,
                                                          ServiceLoader<ResourceBundleProvider> providers,
                                                          CacheKey cacheKey)
    {
        if (providers == null) return null;

        return AccessController.doPrivileged(
                new PrivilegedAction<>() {
                    public ResourceBundle run() {
                        for (Iterator<ResourceBundleProvider> itr = providers.iterator(); itr.hasNext(); ) {
                            try {
                                ResourceBundleProvider provider = itr.next();
                                if (cacheKey != null && cacheKey.callerHasProvider == null
                                        && cacheKey.getModule() == provider.getClass().getModule()) {
                                    cacheKey.callerHasProvider = Boolean.TRUE;
                                }
                                ResourceBundle bundle = provider.getBundle(baseName, locale);
                                trace("provider %s %s locale: %s bundle: %s%n", provider, baseName, locale, bundle);
                                if (bundle != null) {
                                    return bundle;
                                }
                            } catch (ServiceConfigurationError | SecurityException e) {
                                if (cacheKey != null) {
                                    cacheKey.setCause(e);
                                }
                            }
                        }
                        if (cacheKey != null && cacheKey.callerHasProvider == null) {
                            cacheKey.callerHasProvider = Boolean.FALSE;
                        }
                        return null;
                    }
                });

    }

    private static ResourceBundle loadBundle(CacheKey cacheKey,
                                             List<String> formats,
                                             Control control,
                                             boolean reload) {

        Locale targetLocale = cacheKey.getLocale();

        Module module = cacheKey.getModule();
        if (module == null) {

            throw new InternalError(
                "Module for cache key: " + cacheKey + " has been GCed.");
        }
        ClassLoader loader = getLoaderForControl(module);

        ResourceBundle bundle = null;
        for (String format : formats) {
            try {

                bundle = control.newBundle(cacheKey.getName(), targetLocale, format,
                                           loader, reload);
            } catch (LinkageError | Exception error) {

                cacheKey.setCause(error);
            }
            if (bundle != null) {

                cacheKey.setFormat(format);
                bundle.name = cacheKey.getName();
                bundle.locale = targetLocale;

                bundle.expired = false;
                break;
            }
        }

        return bundle;
    }

    private static boolean isValidBundle(ResourceBundle bundle) {
        return bundle != null && bundle != NONEXISTENT_BUNDLE;
    }

    private static boolean hasValidParentChain(ResourceBundle bundle) {
        long now = System.currentTimeMillis();
        while (bundle != null) {
            if (bundle.expired) {
                return false;
            }
            CacheKey key = bundle.cacheKey;
            if (key != null) {
                long expirationTime = key.expirationTime;
                if (expirationTime >= 0 && expirationTime <= now) {
                    return false;
                }
            }
            bundle = bundle.parent;
        }
        return true;
    }

    private static void throwMissingResourceException(String baseName,
                                                      Locale locale,
                                                      Throwable cause) {

        if (cause instanceof MissingResourceException) {
            cause = null;
        }
        throw new MissingResourceException("Can't find bundle for base name "
                                           + baseName + ", locale " + locale,
                                           baseName + "_" + locale,
                                           "",
                                           cause);
    }

    private static ResourceBundle findBundleInCache(CacheKey cacheKey,
                                                    Control control) {
        BundleReference bundleRef = cacheList.get(cacheKey);
        if (bundleRef == null) {
            return null;
        }
        ResourceBundle bundle = bundleRef.get();
        if (bundle == null) {
            return null;
        }
        ResourceBundle p = bundle.parent;
        assert p != NONEXISTENT_BUNDLE;

        if (p != null && p.expired) {
            assert bundle != NONEXISTENT_BUNDLE;
            bundle.expired = true;
            bundle.cacheKey = null;
            cacheList.remove(cacheKey, bundleRef);
            bundle = null;
        } else {
            CacheKey key = bundleRef.getCacheKey();
            long expirationTime = key.expirationTime;
            if (!bundle.expired && expirationTime >= 0 &&
                expirationTime <= System.currentTimeMillis()) {

                if (bundle != NONEXISTENT_BUNDLE) {

                    synchronized (bundle) {
                        expirationTime = key.expirationTime;
                        if (!bundle.expired && expirationTime >= 0 &&
                            expirationTime <= System.currentTimeMillis()) {
                            try {
                                Module module = cacheKey.getModule();
                                bundle.expired =
                                    module == null ||
                                    control.needsReload(key.getName(),
                                                        key.getLocale(),
                                                        key.getFormat(),
                                                        getLoaderForControl(module),
                                                        bundle,
                                                        key.loadTime);
                            } catch (Exception e) {
                                cacheKey.setCause(e);
                            }
                            if (bundle.expired) {

                                bundle.cacheKey = null;
                                cacheList.remove(cacheKey, bundleRef);
                            } else {

                                setExpirationTime(key, control);
                            }
                        }
                    }
                } else {

                    cacheList.remove(cacheKey, bundleRef);
                    bundle = null;
                }
            }
        }
        return bundle;
    }

    private static ResourceBundle putBundleInCache(CacheKey cacheKey,
                                                   ResourceBundle bundle,
                                                   Control control) {
        setExpirationTime(cacheKey, control);
        if (cacheKey.expirationTime != Control.TTL_DONT_CACHE) {
            CacheKey key = new CacheKey(cacheKey);
            BundleReference bundleRef = new BundleReference(bundle, referenceQueue, key);
            bundle.cacheKey = key;

            BundleReference result = cacheList.putIfAbsent(key, bundleRef);

            if (result != null) {
                ResourceBundle rb = result.get();
                if (rb != null && !rb.expired) {

                    bundle.cacheKey = null;
                    bundle = rb;

                    bundleRef.clear();
                } else {

                    cacheList.put(key, bundleRef);
                }
            }
        }
        return bundle;
    }

    private static void setExpirationTime(CacheKey cacheKey, Control control) {
        long ttl = control.getTimeToLive(cacheKey.getName(),
                                         cacheKey.getLocale());
        if (ttl >= 0) {

            long now = System.currentTimeMillis();
            cacheKey.loadTime = now;
            cacheKey.expirationTime = now + ttl;
        } else if (ttl >= Control.TTL_NO_EXPIRATION_CONTROL) {
            cacheKey.expirationTime = ttl;
        } else {
            throw new IllegalArgumentException("Invalid Control: TTL=" + ttl);
        }
    }

    @CallerSensitive
    public static final void clearCache() {
        Class<?> caller = Reflection.getCallerClass();
        cacheList.keySet().removeIf(
            key -> key.getCallerModule() == caller.getModule()
        );
    }

    public static final void clearCache(ClassLoader loader) {
        Objects.requireNonNull(loader);
        cacheList.keySet().removeIf(
            key -> {
                Module m;
                return (m = key.getModule()) != null &&
                       getLoader(m) == loader;
            }
        );
    }

    protected abstract Object handleGetObject(String key);

    public abstract Enumeration<String> getKeys();

    public boolean containsKey(String key) {
        if (key == null) {
            throw new NullPointerException();
        }
        for (ResourceBundle rb = this; rb != null; rb = rb.parent) {
            if (rb.handleKeySet().contains(key)) {
                return true;
            }
        }
        return false;
    }

    public Set<String> keySet() {
        Set<String> keys = new HashSet<>();
        for (ResourceBundle rb = this; rb != null; rb = rb.parent) {
            keys.addAll(rb.handleKeySet());
        }
        return keys;
    }

    protected Set<String> handleKeySet() {
        if (keySet == null) {
            synchronized (this) {
                if (keySet == null) {
                    Set<String> keys = new HashSet<>();
                    Enumeration<String> enumKeys = getKeys();
                    while (enumKeys.hasMoreElements()) {
                        String key = enumKeys.nextElement();
                        if (handleGetObject(key) != null) {
                            keys.add(key);
                        }
                    }
                    keySet = keys;
                }
            }
        }
        return keySet;
    }

    public static class Control {

        public static final List<String> FORMAT_DEFAULT
            = List.of("java.class", "java.properties");

        public static final List<String> FORMAT_CLASS = List.of("java.class");

        public static final List<String> FORMAT_PROPERTIES
            = List.of("java.properties");

        public static final long TTL_DONT_CACHE = -1;

        public static final long TTL_NO_EXPIRATION_CONTROL = -2;

        private static final Control INSTANCE = new Control();

        protected Control() {
        }

        public static final Control getControl(List<String> formats) {
            if (formats.equals(Control.FORMAT_PROPERTIES)) {
                return SingleFormatControl.PROPERTIES_ONLY;
            }
            if (formats.equals(Control.FORMAT_CLASS)) {
                return SingleFormatControl.CLASS_ONLY;
            }
            if (formats.equals(Control.FORMAT_DEFAULT)) {
                return Control.INSTANCE;
            }
            throw new IllegalArgumentException();
        }

        public static final Control getNoFallbackControl(List<String> formats) {
            if (formats.equals(Control.FORMAT_DEFAULT)) {
                return NoFallbackControl.NO_FALLBACK;
            }
            if (formats.equals(Control.FORMAT_PROPERTIES)) {
                return NoFallbackControl.PROPERTIES_ONLY_NO_FALLBACK;
            }
            if (formats.equals(Control.FORMAT_CLASS)) {
                return NoFallbackControl.CLASS_ONLY_NO_FALLBACK;
            }
            throw new IllegalArgumentException();
        }

        public List<String> getFormats(String baseName) {
            if (baseName == null) {
                throw new NullPointerException();
            }
            return FORMAT_DEFAULT;
        }

        public List<Locale> getCandidateLocales(String baseName, Locale locale) {
            if (baseName == null) {
                throw new NullPointerException();
            }
            return new ArrayList<>(CANDIDATES_CACHE.get(locale.getBaseLocale()));
        }

        private static final CandidateListCache CANDIDATES_CACHE = new CandidateListCache();

        private static class CandidateListCache extends LocaleObjectCache<BaseLocale, List<Locale>> {
            protected List<Locale> createObject(BaseLocale base) {
                String language = base.getLanguage();
                String script = base.getScript();
                String region = base.getRegion();
                String variant = base.getVariant();

                boolean isNorwegianBokmal = false;
                boolean isNorwegianNynorsk = false;
                if (language.equals("no")) {
                    if (region.equals("NO") && variant.equals("NY")) {
                        variant = "";
                        isNorwegianNynorsk = true;
                    } else {
                        isNorwegianBokmal = true;
                    }
                }
                if (language.equals("nb") || isNorwegianBokmal) {
                    List<Locale> tmpList = getDefaultList("nb", script, region, variant);

                    List<Locale> bokmalList = new LinkedList<>();
                    for (Locale l : tmpList) {
                        bokmalList.add(l);
                        if (l.getLanguage().isEmpty()) {
                            break;
                        }
                        bokmalList.add(Locale.getInstance("no", l.getScript(), l.getCountry(),
                                l.getVariant(), null));
                    }
                    return bokmalList;
                } else if (language.equals("nn") || isNorwegianNynorsk) {

                    List<Locale> nynorskList = getDefaultList("nn", script, region, variant);
                    int idx = nynorskList.size() - 1;
                    nynorskList.add(idx++, Locale.getInstance("no", "NO", "NY"));
                    nynorskList.add(idx++, Locale.getInstance("no", "NO", ""));
                    nynorskList.add(idx++, Locale.getInstance("no", "", ""));
                    return nynorskList;
                }

                else if (language.equals("zh")) {
                    if (script.isEmpty() && !region.isEmpty()) {

                        switch (region) {
                        case "TW":
                        case "HK":
                        case "MO":
                            script = "Hant";
                            break;
                        case "CN":
                        case "SG":
                            script = "Hans";
                            break;
                        }
                    }
                }

                return getDefaultList(language, script, region, variant);
            }

            private static List<Locale> getDefaultList(String language, String script, String region, String variant) {
                List<String> variants = null;

                if (!variant.isEmpty()) {
                    variants = new LinkedList<>();
                    int idx = variant.length();
                    while (idx != -1) {
                        variants.add(variant.substring(0, idx));
                        idx = variant.lastIndexOf('_', --idx);
                    }
                }

                List<Locale> list = new LinkedList<>();

                if (variants != null) {
                    for (String v : variants) {
                        list.add(Locale.getInstance(language, script, region, v, null));
                    }
                }
                if (!region.isEmpty()) {
                    list.add(Locale.getInstance(language, script, region, "", null));
                }
                if (!script.isEmpty()) {
                    list.add(Locale.getInstance(language, script, "", "", null));

                    if (language.equals("zh")) {
                        if (region.isEmpty()) {

                            switch (script) {
                                case "Hans":
                                    region = "CN";
                                    break;
                                case "Hant":
                                    region = "TW";
                                    break;
                            }
                        }
                    }

                    if (variants != null) {
                        for (String v : variants) {
                            list.add(Locale.getInstance(language, "", region, v, null));
                        }
                    }
                    if (!region.isEmpty()) {
                        list.add(Locale.getInstance(language, "", region, "", null));
                    }
                }
                if (!language.isEmpty()) {
                    list.add(Locale.getInstance(language, "", "", "", null));
                }

                list.add(Locale.ROOT);

                return list;
            }
        }

        public Locale getFallbackLocale(String baseName, Locale locale) {
            if (baseName == null) {
                throw new NullPointerException();
            }
            Locale defaultLocale = Locale.getDefault();
            return locale.equals(defaultLocale) ? null : defaultLocale;
        }

        public ResourceBundle newBundle(String baseName, Locale locale, String format,
                                        ClassLoader loader, boolean reload)
                    throws IllegalAccessException, InstantiationException, IOException {

            String bundleName = toBundleName(baseName, locale);
            ResourceBundle bundle = null;
            if (format.equals("java.class")) {
                try {
                    Class<?> c = loader.loadClass(bundleName);

                    if (ResourceBundle.class.isAssignableFrom(c)) {
                        @SuppressWarnings("unchecked")
                        Class<ResourceBundle> bundleClass = (Class<ResourceBundle>)c;
                        Module m = bundleClass.getModule();

                        if (m.isNamed() && !m.isOpen(bundleClass.getPackageName())) {
                            throw new IllegalAccessException("unnamed module can't load " +
                                bundleClass.getName() + " in " + m.toString());
                        }
                        try {

                            Constructor<ResourceBundle> ctor = bundleClass.getConstructor();
                            if (!Modifier.isPublic(ctor.getModifiers())) {
                                return null;
                            }

                            PrivilegedAction<Void> pa1 = () -> { ctor.setAccessible(true); return null; };
                            AccessController.doPrivileged(pa1);
                            bundle = ctor.newInstance((Object[]) null);
                        } catch (InvocationTargetException e) {
                            uncheckedThrow(e);
                        }
                    } else {
                        throw new ClassCastException(c.getName()
                                + " cannot be cast to ResourceBundle");
                    }
                } catch (ClassNotFoundException|NoSuchMethodException e) {
                }
            } else if (format.equals("java.properties")) {
                final String resourceName = toResourceName0(bundleName, "properties");
                if (resourceName == null) {
                    return bundle;
                }

                final boolean reloadFlag = reload;
                InputStream stream = null;
                try {
                    stream = AccessController.doPrivileged(
                        new PrivilegedExceptionAction<>() {
                            public InputStream run() throws IOException {
                                URL url = loader.getResource(resourceName);
                                if (url == null) return null;

                                URLConnection connection = url.openConnection();
                                if (reloadFlag) {

                                    connection.setUseCaches(false);
                                }
                                return connection.getInputStream();
                            }
                        });
                } catch (PrivilegedActionException e) {
                    throw (IOException) e.getException();
                }
                if (stream != null) {
                    try {
                        bundle = new PropertyResourceBundle(stream);
                    } finally {
                        stream.close();
                    }
                }
            } else {
                throw new IllegalArgumentException("unknown format: " + format);
            }
            return bundle;
        }

        public long getTimeToLive(String baseName, Locale locale) {
            if (baseName == null || locale == null) {
                throw new NullPointerException();
            }
            return TTL_NO_EXPIRATION_CONTROL;
        }

        public boolean needsReload(String baseName, Locale locale,
                                   String format, ClassLoader loader,
                                   ResourceBundle bundle, long loadTime) {
            if (bundle == null) {
                throw new NullPointerException();
            }
            if (format.equals("java.class") || format.equals("java.properties")) {
                format = format.substring(5);
            }
            boolean result = false;
            try {
                String resourceName = toResourceName0(toBundleName(baseName, locale), format);
                if (resourceName == null) {
                    return result;
                }
                URL url = loader.getResource(resourceName);
                if (url != null) {
                    long lastModified = 0;
                    URLConnection connection = url.openConnection();
                    if (connection != null) {

                        connection.setUseCaches(false);
                        if (connection instanceof JarURLConnection) {
                            JarEntry ent = ((JarURLConnection)connection).getJarEntry();
                            if (ent != null) {
                                lastModified = ent.getTime();
                                if (lastModified == -1) {
                                    lastModified = 0;
                                }
                            }
                        } else {
                            lastModified = connection.getLastModified();
                        }
                    }
                    result = lastModified >= loadTime;
                }
            } catch (NullPointerException npe) {
                throw npe;
            } catch (Exception e) {

            }
            return result;
        }

        public String toBundleName(String baseName, Locale locale) {
            if (locale == Locale.ROOT) {
                return baseName;
            }

            String language = locale.getLanguage();
            String script = locale.getScript();
            String country = locale.getCountry();
            String variant = locale.getVariant();

            if (language == "" && country == "" && variant == "") {
                return baseName;
            }

            StringBuilder sb = new StringBuilder(baseName);
            sb.append('_');
            if (script != "") {
                if (variant != "") {
                    sb.append(language).append('_').append(script).append('_').append(country).append('_').append(variant);
                } else if (country != "") {
                    sb.append(language).append('_').append(script).append('_').append(country);
                } else {
                    sb.append(language).append('_').append(script);
                }
            } else {
                if (variant != "") {
                    sb.append(language).append('_').append(country).append('_').append(variant);
                } else if (country != "") {
                    sb.append(language).append('_').append(country);
                } else {
                    sb.append(language);
                }
            }
            return sb.toString();

        }

        public final String toResourceName(String bundleName, String suffix) {
            StringBuilder sb = new StringBuilder(bundleName.length() + 1 + suffix.length());
            sb.append(bundleName.replace('.', '/')).append('.').append(suffix);
            return sb.toString();
        }

        private String toResourceName0(String bundleName, String suffix) {

            if (bundleName.contains("://")) {
                return null;
            } else {
                return toResourceName(bundleName, suffix);
            }
        }
    }

    @SuppressWarnings("unchecked")
    private static <T extends Throwable> void uncheckedThrow(Throwable t) throws T {
        if (t != null)
            throw (T)t;
        else
            throw new Error("Unknown Exception");
    }

    private static class SingleFormatControl extends Control {
        private static final Control PROPERTIES_ONLY
            = new SingleFormatControl(FORMAT_PROPERTIES);

        private static final Control CLASS_ONLY
            = new SingleFormatControl(FORMAT_CLASS);

        private final List<String> formats;

        protected SingleFormatControl(List<String> formats) {
            this.formats = formats;
        }

        public List<String> getFormats(String baseName) {
            if (baseName == null) {
                throw new NullPointerException();
            }
            return formats;
        }
    }

    private static final class NoFallbackControl extends SingleFormatControl {
        private static final Control NO_FALLBACK
            = new NoFallbackControl(FORMAT_DEFAULT);

        private static final Control PROPERTIES_ONLY_NO_FALLBACK
            = new NoFallbackControl(FORMAT_PROPERTIES);

        private static final Control CLASS_ONLY_NO_FALLBACK
            = new NoFallbackControl(FORMAT_CLASS);

        protected NoFallbackControl(List<String> formats) {
            super(formats);
        }

        public Locale getFallbackLocale(String baseName, Locale locale) {
            if (baseName == null || locale == null) {
                throw new NullPointerException();
            }
            return null;
        }
    }

    private static class ResourceBundleProviderHelper {

        static ResourceBundle newResourceBundle(Class<? extends ResourceBundle> bundleClass) {
            try {
                @SuppressWarnings("unchecked")
                Constructor<? extends ResourceBundle> ctor =
                    bundleClass.getConstructor();
                if (!Modifier.isPublic(ctor.getModifiers())) {
                    return null;
                }

                PrivilegedAction<Void> pa = () -> { ctor.setAccessible(true); return null;};
                AccessController.doPrivileged(pa);
                try {
                    return ctor.newInstance((Object[]) null);
                } catch (InvocationTargetException e) {
                    uncheckedThrow(e);
                } catch (InstantiationException | IllegalAccessException e) {
                    throw new InternalError(e);
                }
            } catch (NoSuchMethodException e) {
                throw new InternalError(e);
            }
            return null;
        }

        static ResourceBundle loadResourceBundle(Module callerModule,
                                                 Module module,
                                                 String baseName,
                                                 Locale locale)
        {
            String bundleName = Control.INSTANCE.toBundleName(baseName, locale);
            try {
                PrivilegedAction<Class<?>> pa = () -> Class.forName(module, bundleName);
                Class<?> c = AccessController.doPrivileged(pa, null, GET_CLASSLOADER_PERMISSION);
                trace("local in %s %s caller %s: %s%n", module, bundleName, callerModule, c);

                if (c == null) {

                    ClassLoader loader = getLoader(module);
                    if (loader != null) {
                        c = Class.forName(bundleName, false, loader);
                    } else {
                        c = BootLoader.loadClassOrNull(bundleName);
                    }
                    trace("loader for %s %s caller %s: %s%n", module, bundleName, callerModule, c);
                }

                if (c != null && ResourceBundle.class.isAssignableFrom(c)) {
                    @SuppressWarnings("unchecked")
                    Class<ResourceBundle> bundleClass = (Class<ResourceBundle>) c;
                    Module m = bundleClass.getModule();
                    if (!isAccessible(callerModule, m, bundleClass.getPackageName())) {
                        trace("   %s does not have access to %s/%s%n", callerModule,
                              m.getName(), bundleClass.getPackageName());
                        return null;
                    }

                    return newResourceBundle(bundleClass);
                }
            } catch (ClassNotFoundException e) {}
            return null;
        }

        static boolean isAccessible(Module callerModule, Module module, String pn) {
            if (!module.isNamed() || callerModule == module)
                return true;

            return module.isOpen(pn, callerModule);
        }

        static ResourceBundle loadPropertyResourceBundle(Module callerModule,
                                                         Module module,
                                                         String baseName,
                                                         Locale locale)
            throws IOException
        {
            String bundleName = Control.INSTANCE.toBundleName(baseName, locale);

            PrivilegedAction<InputStream> pa = () -> {
                try {
                    String resourceName = Control.INSTANCE
                        .toResourceName0(bundleName, "properties");
                    if (resourceName == null) {
                        return null;
                    }
                    trace("local in %s %s caller %s%n", module, resourceName, callerModule);

                    String pn = toPackageName(bundleName);
                    trace("   %s/%s is accessible to %s : %s%n",
                            module.getName(), pn, callerModule,
                            isAccessible(callerModule, module, pn));
                    if (isAccessible(callerModule, module, pn)) {
                        InputStream in = module.getResourceAsStream(resourceName);
                        if (in != null) {
                            return in;
                        }
                    }

                    ClassLoader loader = module.getClassLoader();
                    trace("loader for %s %s caller %s%n", module, resourceName, callerModule);

                    try {
                        if (loader != null) {
                            return loader.getResourceAsStream(resourceName);
                        } else {
                            URL url = BootLoader.findResource(resourceName);
                            if (url != null) {
                                return url.openStream();
                            }
                        }
                    } catch (Exception e) {}
                    return null;

                } catch (IOException e) {
                    throw new UncheckedIOException(e);
                }
            };

            try (InputStream stream = AccessController.doPrivileged(pa)) {
                if (stream != null) {
                    return new PropertyResourceBundle(stream);
                } else {
                    return null;
                }
            } catch (UncheckedIOException e) {
                throw e.getCause();
            }
        }

        private static String toPackageName(String bundleName) {
            int i = bundleName.lastIndexOf('.');
            return i != -1 ? bundleName.substring(0, i) : "";
        }

    }

    private static final boolean TRACE_ON = Boolean.valueOf(
        GetPropertyAction.privilegedGetProperty("resource.bundle.debug", "false"));

    private static void trace(String format, Object... params) {
        if (TRACE_ON)
            System.out.format(format, params);
    }
}
