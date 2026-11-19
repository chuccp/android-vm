package java.io;

import java.security.AccessController;
import java.security.PrivilegedAction;
import java.security.Security;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.function.Function;

import jdk.internal.misc.SharedSecrets;
import jdk.internal.util.StaticProperty;

@FunctionalInterface
public interface ObjectInputFilter {

    Status checkInput(FilterInfo filterInfo);

    interface FilterInfo {

        Class<?> serialClass();

        long arrayLength();

        long depth();

        long references();

        long streamBytes();
    }

    enum Status {

        UNDECIDED,

        ALLOWED,

        REJECTED;
    }

    final class Config {

        private Config() {}

        private final static Object serialFilterLock = new Object();

        private final static System.Logger configLog;

        static void filterLog(System.Logger.Level level, String msg, Object... args) {
            if (configLog != null) {
                configLog.log(level, msg, args);
            }
        }

        private final static String SERIAL_FILTER_PROPNAME = "jdk.serialFilter";

        private final static ObjectInputFilter configuredFilter;

        static {
            configuredFilter = AccessController
                    .doPrivileged((PrivilegedAction<ObjectInputFilter>) () -> {
                        String props = StaticProperty.jdkSerialFilter();
                        if (props == null) {
                            props = Security.getProperty(SERIAL_FILTER_PROPNAME);
                        }
                        if (props != null) {
                            System.Logger log =
                                    System.getLogger("java.io.serialization");
                            log.log(System.Logger.Level.INFO,
                                    "Creating serialization filter from {0}", props);
                            try {
                                return createFilter(props);
                            } catch (RuntimeException re) {
                                log.log(System.Logger.Level.ERROR,
                                        "Error configuring filter: {0}", re);
                            }
                        }
                        return null;
                    });
            configLog = (configuredFilter != null) ? System.getLogger("java.io.serialization") : null;

            SharedSecrets.setJavaObjectInputFilterAccess(Config::createFilter2);
        }

        private static volatile ObjectInputFilter serialFilter = configuredFilter;

        public static ObjectInputFilter getSerialFilter() {
            return serialFilter;
        }

        public static void setSerialFilter(ObjectInputFilter filter) {
            Objects.requireNonNull(filter, "filter");
            SecurityManager sm = System.getSecurityManager();
            if (sm != null) {
                sm.checkPermission(ObjectStreamConstants.SERIAL_FILTER_PERMISSION);
            }
            synchronized (serialFilterLock) {
                if (serialFilter != null) {
                    throw new IllegalStateException("Serial filter can only be set once");
                }
                serialFilter = filter;
            }
        }

        public static ObjectInputFilter createFilter(String pattern) {
            Objects.requireNonNull(pattern, "pattern");
            return Global.createFilter(pattern, true);
        }

        static ObjectInputFilter createFilter2(String pattern) {
            Objects.requireNonNull(pattern, "pattern");
            return Global.createFilter(pattern, false);
        }

        final static class Global implements ObjectInputFilter {

            private final String pattern;

            private final List<Function<Class<?>, Status>> filters;

            private long maxStreamBytes;

            private long maxDepth;

            private long maxReferences;

            private long maxArrayLength;

            private final boolean checkComponentType;

            static ObjectInputFilter createFilter(String pattern, boolean checkComponentType) {
                try {
                    return new Global(pattern, checkComponentType);
                } catch (UnsupportedOperationException uoe) {

                    return null;
                }
            }

            private Global(String pattern, boolean checkComponentType) {
                boolean hasLimits = false;
                this.pattern = pattern;
                this.checkComponentType = checkComponentType;

                maxArrayLength = Long.MAX_VALUE;
                maxDepth = Long.MAX_VALUE;
                maxReferences = Long.MAX_VALUE;
                maxStreamBytes = Long.MAX_VALUE;

                String[] patterns = pattern.split(";");
                filters = new ArrayList<>(patterns.length);
                for (int i = 0; i < patterns.length; i++) {
                    String p = patterns[i];
                    int nameLen = p.length();
                    if (nameLen == 0) {
                        continue;
                    }
                    if (parseLimit(p)) {

                        hasLimits = true;
                        continue;
                    }
                    boolean negate = p.charAt(0) == '!';
                    int poffset = negate ? 1 : 0;

                    int slash = p.indexOf('/', poffset);
                    if (slash == poffset) {
                        throw new IllegalArgumentException("module name is missing in: \"" + pattern + "\"");
                    }
                    final String moduleName = (slash >= 0) ? p.substring(poffset, slash) : null;
                    poffset = (slash >= 0) ? slash + 1 : poffset;

                    final Function<Class<?>, Status> patternFilter;
                    if (p.endsWith("*")) {

                        if (p.endsWith(".*")) {

                            final String pkg = p.substring(poffset, nameLen - 2);
                            if (pkg.isEmpty()) {
                                throw new IllegalArgumentException("package missing in: \"" + pattern + "\"");
                            }
                            if (negate) {

                                patternFilter = c -> matchesPackage(c, pkg) ? Status.REJECTED : Status.UNDECIDED;
                            } else {

                                patternFilter = c -> matchesPackage(c, pkg) ? Status.ALLOWED : Status.UNDECIDED;
                            }
                        } else if (p.endsWith(".**")) {

                            final String pkgs = p.substring(poffset, nameLen - 2);
                            if (pkgs.length() < 2) {
                                throw new IllegalArgumentException("package missing in: \"" + pattern + "\"");
                            }
                            if (negate) {

                                patternFilter = c -> c.getName().startsWith(pkgs) ? Status.REJECTED : Status.UNDECIDED;
                            } else {

                                patternFilter = c -> c.getName().startsWith(pkgs) ? Status.ALLOWED : Status.UNDECIDED;
                            }
                        } else {

                            final String className = p.substring(poffset, nameLen - 1);
                            if (negate) {

                                patternFilter = c -> c.getName().startsWith(className) ? Status.REJECTED : Status.UNDECIDED;
                            } else {

                                patternFilter = c -> c.getName().startsWith(className) ? Status.ALLOWED : Status.UNDECIDED;
                            }
                        }
                    } else {
                        final String name = p.substring(poffset);
                        if (name.isEmpty()) {
                            throw new IllegalArgumentException("class or package missing in: \"" + pattern + "\"");
                        }

                        if (negate) {

                            patternFilter = c -> c.getName().equals(name) ? Status.REJECTED : Status.UNDECIDED;
                        } else {

                            patternFilter = c -> c.getName().equals(name) ? Status.ALLOWED : Status.UNDECIDED;
                        }
                    }

                    if (moduleName == null) {
                        filters.add(patternFilter);
                    } else {
                        filters.add(c -> moduleName.equals(c.getModule().getName()) ? patternFilter.apply(c) : Status.UNDECIDED);
                    }
                }
                if (filters.isEmpty() && !hasLimits) {
                    throw new UnsupportedOperationException("no non-empty patterns");
                }
            }

            private boolean parseLimit(String pattern) {
                int eqNdx = pattern.indexOf('=');
                if (eqNdx < 0) {

                    return false;
                }
                String valueString = pattern.substring(eqNdx + 1);
                if (pattern.startsWith("maxdepth=")) {
                    maxDepth = parseValue(valueString);
                } else if (pattern.startsWith("maxarray=")) {
                    maxArrayLength = parseValue(valueString);
                } else if (pattern.startsWith("maxrefs=")) {
                    maxReferences = parseValue(valueString);
                } else if (pattern.startsWith("maxbytes=")) {
                    maxStreamBytes = parseValue(valueString);
                } else {
                    throw new IllegalArgumentException("unknown limit: " + pattern.substring(0, eqNdx));
                }
                return true;
            }

            private static long parseValue(String string) throws IllegalArgumentException {

                long value = Long.parseLong(string);
                if (value < 0) {
                    throw new IllegalArgumentException("negative limit: " + string);
                }
                return value;
            }

            @Override
            public Status checkInput(FilterInfo filterInfo) {
                if (filterInfo.references() < 0
                        || filterInfo.depth() < 0
                        || filterInfo.streamBytes() < 0
                        || filterInfo.references() > maxReferences
                        || filterInfo.depth() > maxDepth
                        || filterInfo.streamBytes() > maxStreamBytes) {
                    return Status.REJECTED;
                }

                Class<?> clazz = filterInfo.serialClass();
                if (clazz != null) {
                    if (clazz.isArray()) {
                        if (filterInfo.arrayLength() >= 0 && filterInfo.arrayLength() > maxArrayLength) {

                            return Status.REJECTED;
                        }
                        if (!checkComponentType) {

                            return Status.UNDECIDED;
                        }
                        do {

                            clazz = clazz.getComponentType();
                        } while (clazz.isArray());
                    }

                    if (clazz.isPrimitive())  {

                        return Status.UNDECIDED;
                    } else {

                        final Class<?> cl = clazz;
                        Optional<Status> status = filters.stream()
                                .map(f -> f.apply(cl))
                                .filter(p -> p != Status.UNDECIDED)
                                .findFirst();
                        return status.orElse(Status.UNDECIDED);
                    }
                }
                return Status.UNDECIDED;
            }

            private static boolean matchesPackage(Class<?> c, String pkg) {
                return pkg.equals(c.getPackageName());
            }

            @Override
            public String toString() {
                return pattern;
            }
        }
    }
}
