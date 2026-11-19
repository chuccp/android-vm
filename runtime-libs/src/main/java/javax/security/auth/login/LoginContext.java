package javax.security.auth.login;

import java.security.AccessController;
import java.security.PrivilegedAction;
import java.util.Map;
import java.util.HashMap;
import java.text.MessageFormat;
import javax.security.auth.Subject;
import javax.security.auth.AuthPermission;
import javax.security.auth.callback.*;
import javax.security.auth.spi.LoginModule;
import java.security.AccessControlContext;
import java.util.ServiceLoader;

import sun.security.util.PendingException;
import sun.security.util.ResourcesMgr;

import java.util.Set;
import java.util.WeakHashMap;
import java.util.stream.*;
import java.util.ServiceLoader.Provider;

public class LoginContext {

    private static final String LOGIN_METHOD            = "login";
    private static final String COMMIT_METHOD           = "commit";
    private static final String ABORT_METHOD            = "abort";
    private static final String LOGOUT_METHOD           = "logout";
    private static final String OTHER                   = "other";
    private static final String DEFAULT_HANDLER         =
                                "auth.login.defaultCallbackHandler";
    private Subject subject = null;
    private boolean subjectProvided = false;
    private boolean loginSucceeded = false;
    private CallbackHandler callbackHandler;
    private Map<String,?> state = new HashMap<String,Object>();

    private Configuration config;
    private AccessControlContext creatorAcc = null;
    private ModuleInfo[] moduleStack;
    private ClassLoader contextClassLoader = null;

    private int moduleIndex = 0;
    private LoginException firstError = null;
    private LoginException firstRequiredError = null;
    private boolean success = false;

    private static final sun.security.util.Debug debug =
        sun.security.util.Debug.getInstance("logincontext", "\t[LoginContext]");
    private static final WeakHashMap<ClassLoader, Set<Provider<LoginModule>>> providersCache =
        new WeakHashMap<>();

    private void init(String name) throws LoginException {

        SecurityManager sm = System.getSecurityManager();
        if (sm != null && creatorAcc == null) {
            sm.checkPermission(new AuthPermission
                                ("createLoginContext." + name));
        }

        if (name == null)
            throw new LoginException
                (ResourcesMgr.getString("Invalid.null.input.name"));

        if (config == null) {
            config = java.security.AccessController.doPrivileged
                (new java.security.PrivilegedAction<Configuration>() {
                public Configuration run() {
                    return Configuration.getConfiguration();
                }
            });
        }

        AppConfigurationEntry[] entries = config.getAppConfigurationEntry(name);
        if (entries == null) {

            if (sm != null && creatorAcc == null) {
                sm.checkPermission(new AuthPermission
                                ("createLoginContext." + OTHER));
            }

            entries = config.getAppConfigurationEntry(OTHER);
            if (entries == null) {
                MessageFormat form = new MessageFormat(ResourcesMgr.getString
                        ("No.LoginModules.configured.for.name"));
                Object[] source = {name};
                throw new LoginException(form.format(source));
            }
        }
        moduleStack = new ModuleInfo[entries.length];
        for (int i = 0; i < entries.length; i++) {

            moduleStack[i] = new ModuleInfo
                                (new AppConfigurationEntry
                                        (entries[i].getLoginModuleName(),
                                        entries[i].getControlFlag(),
                                        entries[i].getOptions()),
                                null);
        }

        contextClassLoader = java.security.AccessController.doPrivileged
                (new java.security.PrivilegedAction<ClassLoader>() {
                public ClassLoader run() {
                    ClassLoader loader =
                            Thread.currentThread().getContextClassLoader();
                    if (loader == null) {

                        loader = ClassLoader.getSystemClassLoader();
                    }

                    return loader;
                }
        });

    }

    private void loadDefaultCallbackHandler() throws LoginException {

        try {

            final ClassLoader finalLoader = contextClassLoader;

            this.callbackHandler = java.security.AccessController.doPrivileged(
                new java.security.PrivilegedExceptionAction<CallbackHandler>() {
                public CallbackHandler run() throws Exception {
                    String defaultHandler = java.security.Security.getProperty
                        (DEFAULT_HANDLER);
                    if (defaultHandler == null || defaultHandler.isEmpty())
                        return null;
                    Class<? extends CallbackHandler> c = Class.forName(
                            defaultHandler, true,
                            finalLoader).asSubclass(CallbackHandler.class);
                    @SuppressWarnings("deprecation")
                    CallbackHandler result = c.newInstance();
                    return result;
                }
            });
        } catch (java.security.PrivilegedActionException pae) {
            throw new LoginException(pae.getException().toString());
        }

        if (this.callbackHandler != null && creatorAcc == null) {
            this.callbackHandler = new SecureCallbackHandler
                                (java.security.AccessController.getContext(),
                                this.callbackHandler);
        }
    }

    public LoginContext(String name) throws LoginException {
        init(name);
        loadDefaultCallbackHandler();
    }

    public LoginContext(String name, Subject subject)
    throws LoginException {
        init(name);
        if (subject == null)
            throw new LoginException
                (ResourcesMgr.getString("invalid.null.Subject.provided"));
        this.subject = subject;
        subjectProvided = true;
        loadDefaultCallbackHandler();
    }

    public LoginContext(String name, CallbackHandler callbackHandler)
    throws LoginException {
        init(name);
        if (callbackHandler == null)
            throw new LoginException(ResourcesMgr.getString
                                ("invalid.null.CallbackHandler.provided"));
        this.callbackHandler = new SecureCallbackHandler
                                (java.security.AccessController.getContext(),
                                callbackHandler);
    }

    public LoginContext(String name, Subject subject,
                        CallbackHandler callbackHandler) throws LoginException {
        this(name, subject);
        if (callbackHandler == null)
            throw new LoginException(ResourcesMgr.getString
                                ("invalid.null.CallbackHandler.provided"));
        this.callbackHandler = new SecureCallbackHandler
                                (java.security.AccessController.getContext(),
                                callbackHandler);
    }

    public LoginContext(String name, Subject subject,
                        CallbackHandler callbackHandler,
                        Configuration config) throws LoginException {
        this.config = config;
        if (config != null) {
            creatorAcc = java.security.AccessController.getContext();
        }

        init(name);
        if (subject != null) {
            this.subject = subject;
            subjectProvided = true;
        }
        if (callbackHandler == null) {
            loadDefaultCallbackHandler();
        } else if (creatorAcc == null) {
            this.callbackHandler = new SecureCallbackHandler
                                (java.security.AccessController.getContext(),
                                callbackHandler);
        } else {
            this.callbackHandler = callbackHandler;
        }
    }

    public void login() throws LoginException {

        loginSucceeded = false;

        if (subject == null) {
            subject = new Subject();
        }

        try {

            invokePriv(LOGIN_METHOD);
            invokePriv(COMMIT_METHOD);
            loginSucceeded = true;
        } catch (LoginException le) {
            try {
                invokePriv(ABORT_METHOD);
            } catch (LoginException le2) {
                throw le;
            }
            throw le;
        }
    }

    public void logout() throws LoginException {
        if (subject == null) {
            throw new LoginException(ResourcesMgr.getString
                ("null.subject.logout.called.before.login"));
        }

        invokePriv(LOGOUT_METHOD);
    }

    public Subject getSubject() {
        if (!loginSucceeded && !subjectProvided)
            return null;
        return subject;
    }

    private void clearState() {
        moduleIndex = 0;
        firstError = null;
        firstRequiredError = null;
        success = false;
    }

    private void throwException(LoginException originalError, LoginException le)
    throws LoginException {

        clearState();

        LoginException error = (originalError != null) ? originalError : le;
        throw error;
    }

    private void invokePriv(final String methodName) throws LoginException {
        try {
            java.security.AccessController.doPrivileged
                (new java.security.PrivilegedExceptionAction<Void>() {
                public Void run() throws LoginException {
                    invoke(methodName);
                    return null;
                }
            }, creatorAcc);
        } catch (java.security.PrivilegedActionException pae) {
            throw (LoginException)pae.getException();
        }
    }

    private void invoke(String methodName) throws LoginException {

        for (int i = moduleIndex; i < moduleStack.length; i++, moduleIndex++) {
            String name = moduleStack[i].entry.getLoginModuleName();
            try {

                if (moduleStack[i].module == null) {

                    Set<Provider<LoginModule>> lmProviders;
                    synchronized(providersCache){
                        lmProviders = providersCache.get(contextClassLoader);
                        if (lmProviders == null){
                            if (debug != null){
                                debug.println("Build ServiceProviders cache for ClassLoader: " + contextClassLoader.getName());
                            }
                            ServiceLoader<LoginModule> sc = AccessController.doPrivileged(
                                    (PrivilegedAction<ServiceLoader<LoginModule>>)
                                            () -> java.util.ServiceLoader.load(
                                                LoginModule.class, contextClassLoader));
                            lmProviders = sc.stream().collect(Collectors.toSet());
                                if (debug != null){
                                    debug.println("Discovered ServiceProviders for ClassLoader: " + contextClassLoader.getName());
                                    lmProviders.forEach(System.err::println);
                                }
                            providersCache.put(contextClassLoader,lmProviders);
                        }
                    }
                    for (Provider<LoginModule> lm: lmProviders){
                        if (lm.type().getName().equals(name)){
                            moduleStack[i].module = lm.get();
                            if (debug != null) {
                                debug.println(name + " loaded as a service");
                            }
                            break;
                        }
                    }
                    if (moduleStack[i].module == null) {
                        try {
                            @SuppressWarnings("deprecation")
                            Object tmp = Class.forName(name, false, contextClassLoader).newInstance();
                            moduleStack[i].module = (LoginModule) tmp;
                            if (debug != null) {
                                debug.println(name + " loaded via reflection");
                            }
                        } catch (ClassNotFoundException e) {
                            throw new LoginException("No LoginModule found for "
                                    + name);
                        }
                    }

                    moduleStack[i].module.initialize(subject,
                            callbackHandler,
                            state,
                            moduleStack[i].entry.getOptions());
                }

                boolean status;
                switch (methodName) {
                    case LOGIN_METHOD:
                        status = moduleStack[i].module.login();
                        break;
                    case COMMIT_METHOD:
                        status = moduleStack[i].module.commit();
                        break;
                    case LOGOUT_METHOD:
                        status = moduleStack[i].module.logout();
                        break;
                    case ABORT_METHOD:
                        status = moduleStack[i].module.abort();
                        break;
                    default:
                        throw new AssertionError("Unknown method " + methodName);
                }

                if (status == true) {

                    if (!methodName.equals(ABORT_METHOD) &&
                        !methodName.equals(LOGOUT_METHOD) &&
                        moduleStack[i].entry.getControlFlag() ==
                    AppConfigurationEntry.LoginModuleControlFlag.SUFFICIENT &&
                        firstRequiredError == null) {

                        clearState();

                        if (debug != null)
                            debug.println(name + " " + methodName + " SUFFICIENT success");
                        return;
                    }

                    if (debug != null)
                        debug.println(name + " " + methodName + " success");
                    success = true;
                } else {
                    if (debug != null)
                        debug.println(name + " " + methodName + " ignored");
                }
            } catch (Exception ite) {

                LoginException le;

                if (ite instanceof PendingException &&
                    methodName.equals(LOGIN_METHOD)) {

                    throw (PendingException)ite;

                } else if (ite instanceof LoginException) {

                    le = (LoginException)ite;

                } else if (ite instanceof SecurityException) {

                    le = new LoginException("Security Exception");
                    le.initCause(new SecurityException());
                    if (debug != null) {
                        debug.println
                            ("original security exception with detail msg " +
                            "replaced by new exception with empty detail msg");
                        debug.println("original security exception: " +
                                ite.toString());
                    }
                } else {

                    java.io.StringWriter sw = new java.io.StringWriter();
                    ite.printStackTrace
                            (new java.io.PrintWriter(sw));
                    sw.flush();
                    le = new LoginException(sw.toString());
                }

                if (moduleStack[i].entry.getControlFlag() ==
                    AppConfigurationEntry.LoginModuleControlFlag.REQUISITE) {

                    if (debug != null)
                        debug.println(name + " " + methodName + " REQUISITE failure");

                    if (methodName.equals(ABORT_METHOD) ||
                        methodName.equals(LOGOUT_METHOD)) {
                        if (firstRequiredError == null)
                            firstRequiredError = le;
                    } else {
                        throwException(firstRequiredError, le);
                    }

                } else if (moduleStack[i].entry.getControlFlag() ==
                    AppConfigurationEntry.LoginModuleControlFlag.REQUIRED) {

                    if (debug != null)
                        debug.println(name + " " + methodName + " REQUIRED failure");

                    if (firstRequiredError == null)
                        firstRequiredError = le;

                } else {

                    if (debug != null)
                        debug.println(name + " " + methodName + " OPTIONAL failure");

                    if (firstError == null)
                        firstError = le;
                }
            }
        }

        if (firstRequiredError != null) {

            throwException(firstRequiredError, null);
        } else if (success == false && firstError != null) {

            throwException(firstError, null);
        } else if (success == false) {

            throwException(new LoginException
                (ResourcesMgr.getString("Login.Failure.all.modules.ignored")),
                null);
        } else {

            clearState();
            return;
        }
    }

    private static class SecureCallbackHandler implements CallbackHandler {

        private final java.security.AccessControlContext acc;
        private final CallbackHandler ch;

        SecureCallbackHandler(java.security.AccessControlContext acc,
                        CallbackHandler ch) {
            this.acc = acc;
            this.ch = ch;
        }

        public void handle(final Callback[] callbacks)
                throws java.io.IOException, UnsupportedCallbackException {
            try {
                java.security.AccessController.doPrivileged
                    (new java.security.PrivilegedExceptionAction<Void>() {
                    public Void run() throws java.io.IOException,
                                        UnsupportedCallbackException {
                        ch.handle(callbacks);
                        return null;
                    }
                }, acc);
            } catch (java.security.PrivilegedActionException pae) {
                if (pae.getException() instanceof java.io.IOException) {
                    throw (java.io.IOException)pae.getException();
                } else {
                    throw (UnsupportedCallbackException)pae.getException();
                }
            }
        }
    }

    private static class ModuleInfo {
        AppConfigurationEntry entry;
        LoginModule module;

        ModuleInfo(AppConfigurationEntry newEntry, LoginModule newModule) {
            this.entry = newEntry;
            this.module = newModule;
        }
    }
}
