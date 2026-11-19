package java.nio.charset;

import jdk.internal.misc.VM;
import sun.nio.cs.ThreadLocalCoders;
import sun.security.action.GetPropertyAction;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.spi.CharsetProvider;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Locale;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Objects;
import java.util.ServiceConfigurationError;
import java.util.ServiceLoader;
import java.util.Set;
import java.util.SortedMap;
import java.util.TreeMap;

public abstract class Charset
    implements Comparable<Charset>
{

    private static void checkName(String s) {
        int n = s.length();
        if (n == 0) {
            throw new IllegalCharsetNameException(s);
        }
        for (int i = 0; i < n; i++) {
            char c = s.charAt(i);
            if (c >= 'A' && c <= 'Z') continue;
            if (c >= 'a' && c <= 'z') continue;
            if (c >= '0' && c <= '9') continue;
            if (c == '-' && i != 0) continue;
            if (c == '+' && i != 0) continue;
            if (c == ':' && i != 0) continue;
            if (c == '_' && i != 0) continue;
            if (c == '.' && i != 0) continue;
            throw new IllegalCharsetNameException(s);
        }
    }

    private static final CharsetProvider standardProvider
        = new sun.nio.cs.StandardCharsets();

    private static final String[] zeroAliases = new String[0];

    private static volatile Object[] cache1;
    private static volatile Object[] cache2;

    private static void cache(String charsetName, Charset cs) {
        cache2 = cache1;
        cache1 = new Object[] { charsetName, cs };
    }

    private static Iterator<CharsetProvider> providers() {
        return new Iterator<>() {
                ClassLoader cl = ClassLoader.getSystemClassLoader();
                ServiceLoader<CharsetProvider> sl =
                    ServiceLoader.load(CharsetProvider.class, cl);
                Iterator<CharsetProvider> i = sl.iterator();
                CharsetProvider next = null;

                private boolean getNext() {
                    while (next == null) {
                        try {
                            if (!i.hasNext())
                                return false;
                            next = i.next();
                        } catch (ServiceConfigurationError sce) {
                            if (sce.getCause() instanceof SecurityException) {

                                continue;
                            }
                            throw sce;
                        }
                    }
                    return true;
                }

                public boolean hasNext() {
                    return getNext();
                }

                public CharsetProvider next() {
                    if (!getNext())
                        throw new NoSuchElementException();
                    CharsetProvider n = next;
                    next = null;
                    return n;
                }

                public void remove() {
                    throw new UnsupportedOperationException();
                }

            };
    }

    private static ThreadLocal<ThreadLocal<?>> gate =
            new ThreadLocal<ThreadLocal<?>>();

    private static Charset lookupViaProviders(final String charsetName) {

        if (!VM.isBooted())
            return null;

        if (gate.get() != null)

            return null;
        try {
            gate.set(gate);

            return AccessController.doPrivileged(
                new PrivilegedAction<>() {
                    public Charset run() {
                        for (Iterator<CharsetProvider> i = providers();
                             i.hasNext();) {
                            CharsetProvider cp = i.next();
                            Charset cs = cp.charsetForName(charsetName);
                            if (cs != null)
                                return cs;
                        }
                        return null;
                    }
                });

        } finally {
            gate.set(null);
        }
    }

    private static class ExtendedProviderHolder {
        static final CharsetProvider[] extendedProviders = extendedProviders();

        private static CharsetProvider[] extendedProviders() {
            return AccessController.doPrivileged(new PrivilegedAction<>() {
                    public CharsetProvider[] run() {
                        CharsetProvider[] cps = new CharsetProvider[1];
                        int n = 0;
                        ServiceLoader<CharsetProvider> sl =
                            ServiceLoader.loadInstalled(CharsetProvider.class);
                        for (CharsetProvider cp : sl) {
                            if (n + 1 > cps.length) {
                                cps = Arrays.copyOf(cps, cps.length << 1);
                            }
                            cps[n++] = cp;
                        }
                        return n == cps.length ? cps : Arrays.copyOf(cps, n);
                    }});
        }
    }

    private static Charset lookupExtendedCharset(String charsetName) {
        if (!VM.isBooted())
            return null;
        CharsetProvider[] ecps = ExtendedProviderHolder.extendedProviders;
        for (CharsetProvider cp : ecps) {
            Charset cs = cp.charsetForName(charsetName);
            if (cs != null)
                return cs;
        }
        return null;
    }

    private static Charset lookup(String charsetName) {
        if (charsetName == null)
            throw new IllegalArgumentException("Null charset name");
        Object[] a;
        if ((a = cache1) != null && charsetName.equals(a[0]))
            return (Charset)a[1];

        return lookup2(charsetName);
    }

    private static Charset lookup2(String charsetName) {
        Object[] a;
        if ((a = cache2) != null && charsetName.equals(a[0])) {
            cache2 = cache1;
            cache1 = a;
            return (Charset)a[1];
        }
        Charset cs;
        if ((cs = standardProvider.charsetForName(charsetName)) != null ||
            (cs = lookupExtendedCharset(charsetName))           != null ||
            (cs = lookupViaProviders(charsetName))              != null)
        {
            cache(charsetName, cs);
            return cs;
        }

        checkName(charsetName);
        return null;
    }

    public static boolean isSupported(String charsetName) {
        return (lookup(charsetName) != null);
    }

    public static Charset forName(String charsetName) {
        Charset cs = lookup(charsetName);
        if (cs != null)
            return cs;
        throw new UnsupportedCharsetException(charsetName);
    }

    private static void put(Iterator<Charset> i, Map<String,Charset> m) {
        while (i.hasNext()) {
            Charset cs = i.next();
            if (!m.containsKey(cs.name()))
                m.put(cs.name(), cs);
        }
    }

    public static SortedMap<String,Charset> availableCharsets() {
        return AccessController.doPrivileged(
            new PrivilegedAction<>() {
                public SortedMap<String,Charset> run() {
                    TreeMap<String,Charset> m =
                        new TreeMap<>(
                            String.CASE_INSENSITIVE_ORDER);
                    put(standardProvider.charsets(), m);
                    CharsetProvider[] ecps = ExtendedProviderHolder.extendedProviders;
                    for (CharsetProvider ecp :ecps) {
                        put(ecp.charsets(), m);
                    }
                    for (Iterator<CharsetProvider> i = providers(); i.hasNext();) {
                        CharsetProvider cp = i.next();
                        put(cp.charsets(), m);
                    }
                    return Collections.unmodifiableSortedMap(m);
                }
            });
    }

    private static volatile Charset defaultCharset;

    public static Charset defaultCharset() {
        if (defaultCharset == null) {
            synchronized (Charset.class) {
                String csn = GetPropertyAction
                        .privilegedGetProperty("file.encoding");
                Charset cs = lookup(csn);
                if (cs != null)
                    defaultCharset = cs;
                else
                    defaultCharset = sun.nio.cs.UTF_8.INSTANCE;
            }
        }
        return defaultCharset;
    }

    private final String name;
    private final String[] aliases;
    private Set<String> aliasSet = null;

    protected Charset(String canonicalName, String[] aliases) {
        String[] as = Objects.requireNonNullElse(aliases, zeroAliases);

        if (canonicalName != "ISO-8859-1"
                && canonicalName != "US-ASCII"
                && canonicalName != "UTF-8") {
            checkName(canonicalName);
            for (int i = 0; i < as.length; i++) {
                checkName(as[i]);
            }
        }
        this.name = canonicalName;
        this.aliases = as;
    }

    public final String name() {
        return name;
    }

    public final Set<String> aliases() {
        if (aliasSet != null)
            return aliasSet;
        int n = aliases.length;
        HashSet<String> hs = new HashSet<>(n);
        for (int i = 0; i < n; i++)
            hs.add(aliases[i]);
        aliasSet = Collections.unmodifiableSet(hs);
        return aliasSet;
    }

    public String displayName() {
        return name;
    }

    public final boolean isRegistered() {
        return !name.startsWith("X-") && !name.startsWith("x-");
    }

    public String displayName(Locale locale) {
        return name;
    }

    public abstract boolean contains(Charset cs);

    public abstract CharsetDecoder newDecoder();

    public abstract CharsetEncoder newEncoder();

    public boolean canEncode() {
        return true;
    }

    public final CharBuffer decode(ByteBuffer bb) {
        try {
            return ThreadLocalCoders.decoderFor(this)
                .onMalformedInput(CodingErrorAction.REPLACE)
                .onUnmappableCharacter(CodingErrorAction.REPLACE)
                .decode(bb);
        } catch (CharacterCodingException x) {
            throw new Error(x);
        }
    }

    public final ByteBuffer encode(CharBuffer cb) {
        try {
            return ThreadLocalCoders.encoderFor(this)
                .onMalformedInput(CodingErrorAction.REPLACE)
                .onUnmappableCharacter(CodingErrorAction.REPLACE)
                .encode(cb);
        } catch (CharacterCodingException x) {
            throw new Error(x);
        }
    }

    public final ByteBuffer encode(String str) {
        return encode(CharBuffer.wrap(str));
    }

    public final int compareTo(Charset that) {
        return (name().compareToIgnoreCase(that.name()));
    }

    public final int hashCode() {
        return name().hashCode();
    }

    public final boolean equals(Object ob) {
        if (!(ob instanceof Charset))
            return false;
        if (this == ob)
            return true;
        return name.equals(((Charset)ob).name());
    }

    public final String toString() {
        return name();
    }

}
