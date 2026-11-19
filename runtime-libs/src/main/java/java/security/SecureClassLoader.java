package java.security;

import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;

import sun.security.util.Debug;

public class SecureClassLoader extends ClassLoader {

    private final boolean initialized;

    private final Map<CodeSourceKey, ProtectionDomain> pdcache
            = new ConcurrentHashMap<>(11);

    static {
        ClassLoader.registerAsParallelCapable();
    }

    protected SecureClassLoader(ClassLoader parent) {
        super(parent);

        SecurityManager security = System.getSecurityManager();
        if (security != null) {
            security.checkCreateClassLoader();
        }
        initialized = true;
    }

    protected SecureClassLoader() {
        super();

        SecurityManager security = System.getSecurityManager();
        if (security != null) {
            security.checkCreateClassLoader();
        }
        initialized = true;
    }

    protected SecureClassLoader(String name, ClassLoader parent) {
        super(name, parent);
        SecurityManager security = System.getSecurityManager();
        if (security != null) {
            security.checkCreateClassLoader();
        }
        initialized = true;
    }

    protected final Class<?> defineClass(String name,
                                         byte[] b, int off, int len,
                                         CodeSource cs)
    {
        return defineClass(name, b, off, len, getProtectionDomain(cs));
    }

    protected final Class<?> defineClass(String name, java.nio.ByteBuffer b,
                                         CodeSource cs)
    {
        return defineClass(name, b, getProtectionDomain(cs));
    }

    protected PermissionCollection getPermissions(CodeSource codesource)
    {
        check();
        return new Permissions();
    }

    private static class DebugHolder {
        private static final Debug debug = Debug.getInstance("scl");
    }

    private ProtectionDomain getProtectionDomain(CodeSource cs) {
        if (cs == null) {
            return null;
        }

        CodeSourceKey key = new CodeSourceKey(cs);
        return pdcache.computeIfAbsent(key, new Function<>() {
            @Override
            public ProtectionDomain apply(CodeSourceKey key ) {
                PermissionCollection perms
                        = SecureClassLoader.this.getPermissions(cs);
                ProtectionDomain pd = new ProtectionDomain(
                        cs, perms, SecureClassLoader.this, null);
                if (DebugHolder.debug != null) {
                    DebugHolder.debug.println(" getPermissions " + pd);
                    DebugHolder.debug.println("");
                }
                return pd;
            }
        });
    }

    private void check() {
        if (!initialized) {
            throw new SecurityException("ClassLoader object not initialized");
        }
    }

    private static class CodeSourceKey {
        private final CodeSource cs;

        CodeSourceKey(CodeSource cs) {
            this.cs = cs;
        }

        @Override
        public int hashCode() {
            String locationNoFrag = cs.getLocationNoFragString();
            return locationNoFrag != null ? locationNoFrag.hashCode() : 0;
        }

        @Override
        public boolean equals(Object obj) {
            if (obj == this) {
                return true;
            }

            if (!(obj instanceof CodeSourceKey)) {
                return false;
            }

            CodeSourceKey csk = (CodeSourceKey) obj;

            if (!Objects.equals(cs.getLocationNoFragString(),
                                csk.cs.getLocationNoFragString())) {
                return false;
            }

            return cs.matchCerts(csk.cs, true);
        }
    }
}
