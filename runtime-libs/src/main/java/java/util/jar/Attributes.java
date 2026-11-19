package java.util.jar;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.Collection;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

import sun.nio.cs.UTF_8;
import sun.nio.cs.ISO_8859_1;
import sun.util.logging.PlatformLogger;

public class Attributes implements Map<Object,Object>, Cloneable {

    protected Map<Object,Object> map;

    public Attributes() {
        this(11);
    }

    public Attributes(int size) {
        map = new LinkedHashMap<>(size);
    }

    public Attributes(Attributes attr) {
        map = new LinkedHashMap<>(attr);
    }

    public Object get(Object name) {
        return map.get(name);
    }

    public String getValue(String name) {
        return (String)get(Name.of(name));
    }

    public String getValue(Name name) {
        return (String)get(name);
    }

    public Object put(Object name, Object value) {
        return map.put((Attributes.Name)name, (String)value);
    }

    public String putValue(String name, String value) {
        return (String)put(Name.of(name), value);
    }

    public Object remove(Object name) {
        return map.remove(name);
    }

    public boolean containsValue(Object value) {
        return map.containsValue(value);
    }

    public boolean containsKey(Object name) {
        return map.containsKey(name);
    }

    public void putAll(Map<?,?> attr) {

        if (!Attributes.class.isInstance(attr))
            throw new ClassCastException();
        for (Map.Entry<?,?> me : (attr).entrySet())
            put(me.getKey(), me.getValue());
    }

    public void clear() {
        map.clear();
    }

    public int size() {
        return map.size();
    }

    public boolean isEmpty() {
        return map.isEmpty();
    }

    public Set<Object> keySet() {
        return map.keySet();
    }

    public Collection<Object> values() {
        return map.values();
    }

    public Set<Map.Entry<Object,Object>> entrySet() {
        return map.entrySet();
    }

    public boolean equals(Object o) {
        return map.equals(o);
    }

    public int hashCode() {
        return map.hashCode();
    }

    public Object clone() {
        return new Attributes(this);
    }

     @SuppressWarnings("deprecation")
     void write(DataOutputStream os) throws IOException {
         for (Entry<Object, Object> e : entrySet()) {
             StringBuffer buffer = new StringBuffer(
                                         ((Name) e.getKey()).toString());
             buffer.append(": ");

             String value = (String) e.getValue();
             if (value != null) {
                 byte[] vb = value.getBytes(UTF_8.INSTANCE);
                 value = new String(vb, 0, 0, vb.length);
             }
             buffer.append(value);

             Manifest.make72Safe(buffer);
             buffer.append("\r\n");
             os.writeBytes(buffer.toString());
         }
        os.writeBytes("\r\n");
    }

    @SuppressWarnings("deprecation")
    void writeMain(DataOutputStream out) throws IOException
    {

        String vername = Name.MANIFEST_VERSION.toString();
        String version = getValue(vername);
        if (version == null) {
            vername = Name.SIGNATURE_VERSION.toString();
            version = getValue(vername);
        }

        if (version != null) {
            out.writeBytes(vername+": "+version+"\r\n");
        }

        for (Entry<Object, Object> e : entrySet()) {
            String name = ((Name) e.getKey()).toString();
            if ((version != null) && !(name.equalsIgnoreCase(vername))) {

                StringBuffer buffer = new StringBuffer(name);
                buffer.append(": ");

                String value = (String) e.getValue();
                if (value != null) {
                    byte[] vb = value.getBytes(UTF_8.INSTANCE);
                    value = new String(vb, 0, 0, vb.length);
                }
                buffer.append(value);

                Manifest.make72Safe(buffer);
                buffer.append("\r\n");
                out.writeBytes(buffer.toString());
            }
        }
        out.writeBytes("\r\n");
    }

    @SuppressWarnings("deprecation")
    void read(Manifest.FastInputStream is, byte[] lbuf) throws IOException {
        String name = null, value;
        ByteArrayOutputStream fullLine = new ByteArrayOutputStream();

        int len;
        while ((len = is.readLine(lbuf)) != -1) {
            boolean lineContinued = false;
            byte c = lbuf[--len];
            if (c != '\n' && c != '\r') {
                throw new IOException("line too long");
            }
            if (len > 0 && lbuf[len-1] == '\r') {
                --len;
            }
            if (len == 0) {
                break;
            }
            int i = 0;
            if (lbuf[0] == ' ') {

                if (name == null) {
                    throw new IOException("misplaced continuation line");
                }
                lineContinued = true;
                fullLine.write(lbuf, 1, len - 1);
                if (is.peek() == ' ') {
                    continue;
                }
                value = fullLine.toString(UTF_8.INSTANCE);
                fullLine.reset();
            } else {
                while (lbuf[i++] != ':') {
                    if (i >= len) {
                        throw new IOException("invalid header field");
                    }
                }
                if (lbuf[i++] != ' ') {
                    throw new IOException("invalid header field");
                }
                name = new String(lbuf, 0, 0, i - 2);
                if (is.peek() == ' ') {
                    fullLine.reset();
                    fullLine.write(lbuf, i, len - i);
                    continue;
                }
                value = new String(lbuf, i, len - i, UTF_8.INSTANCE);
            }
            try {
                if ((putValue(name, value) != null) && (!lineContinued)) {
                    PlatformLogger.getLogger("java.util.jar").warning(
                                     "Duplicate name in Manifest: " + name
                                     + ".\n"
                                     + "Ensure that the manifest does not "
                                     + "have duplicate entries, and\n"
                                     + "that blank lines separate "
                                     + "individual sections in both your\n"
                                     + "manifest and in the META-INF/MANIFEST.MF "
                                     + "entry in the jar file.");
                }
            } catch (IllegalArgumentException e) {
                throw new IOException("invalid header field name: " + name);
            }
        }
    }

    public static class Name {
        private final String name;
        private final int hashCode;

        private static final Map<String, Name> KNOWN_NAMES;

        static final Name of(String name) {
            Name n = KNOWN_NAMES.get(name);
            if (n != null) {
                return n;
            }
            return new Name(name);
        }

        public Name(String name) {
            this.hashCode = hash(name);
            this.name = name.intern();
        }

        private final int hash(String name) {
            Objects.requireNonNull(name, "name");
            int len = name.length();
            if (len > 70 || len == 0) {
                throw new IllegalArgumentException(name);
            }

            int h = 0;
            for (int i = 0; i < len; i++) {
                char c = name.charAt(i);
                if (c >= 'a' && c <= 'z') {

                    h = h * 31 + (c - 0x20);
                } else if ((c >= 'A' && c <= 'Z' ||
                        c >= '0' && c <= '9' ||
                        c == '_' || c == '-')) {
                    h = h * 31 + c;
                } else {
                    throw new IllegalArgumentException(name);
                }
            }
            return h;
        }

        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (o instanceof Name) {
                Name other = (Name)o;
                return other.name.equalsIgnoreCase(name);
            } else {
                return false;
            }
        }

        public int hashCode() {
            return hashCode;
        }

        public String toString() {
            return name;
        }

        public static final Name MANIFEST_VERSION = new Name("Manifest-Version");

        public static final Name SIGNATURE_VERSION = new Name("Signature-Version");

        public static final Name CONTENT_TYPE = new Name("Content-Type");

        public static final Name CLASS_PATH = new Name("Class-Path");

        public static final Name MAIN_CLASS = new Name("Main-Class");

        public static final Name SEALED = new Name("Sealed");

        public static final Name EXTENSION_LIST = new Name("Extension-List");

        public static final Name EXTENSION_NAME = new Name("Extension-Name");

        @Deprecated
        public static final Name EXTENSION_INSTALLATION = new Name("Extension-Installation");

        public static final Name IMPLEMENTATION_TITLE = new Name("Implementation-Title");

        public static final Name IMPLEMENTATION_VERSION = new Name("Implementation-Version");

        public static final Name IMPLEMENTATION_VENDOR = new Name("Implementation-Vendor");

        @Deprecated
        public static final Name IMPLEMENTATION_VENDOR_ID = new Name("Implementation-Vendor-Id");

        @Deprecated
        public static final Name IMPLEMENTATION_URL = new Name("Implementation-URL");

        public static final Name SPECIFICATION_TITLE = new Name("Specification-Title");

        public static final Name SPECIFICATION_VERSION = new Name("Specification-Version");

        public static final Name SPECIFICATION_VENDOR = new Name("Specification-Vendor");

        public static final Name MULTI_RELEASE = new Name("Multi-Release");

        private static void addName(Map<String, Name> names, Name name) {
            names.put(name.name, name);
        }

        static {
            var names = new HashMap<String, Name>(64);
            addName(names, MANIFEST_VERSION);
            addName(names, SIGNATURE_VERSION);
            addName(names, CONTENT_TYPE);
            addName(names, CLASS_PATH);
            addName(names, MAIN_CLASS);
            addName(names, SEALED);
            addName(names, EXTENSION_LIST);
            addName(names, EXTENSION_NAME);
            addName(names, IMPLEMENTATION_TITLE);
            addName(names, IMPLEMENTATION_VERSION);
            addName(names, IMPLEMENTATION_VENDOR);
            addName(names, SPECIFICATION_TITLE);
            addName(names, SPECIFICATION_VERSION);
            addName(names, SPECIFICATION_VENDOR);
            addName(names, MULTI_RELEASE);

            addName(names, new Name("Add-Exports"));
            addName(names, new Name("Add-Opens"));
            addName(names, new Name("Ant-Version"));
            addName(names, new Name("Archiver-Version"));
            addName(names, new Name("Build-Jdk"));
            addName(names, new Name("Built-By"));
            addName(names, new Name("Bnd-LastModified"));
            addName(names, new Name("Bundle-Description"));
            addName(names, new Name("Bundle-DocURL"));
            addName(names, new Name("Bundle-License"));
            addName(names, new Name("Bundle-ManifestVersion"));
            addName(names, new Name("Bundle-Name"));
            addName(names, new Name("Bundle-Vendor"));
            addName(names, new Name("Bundle-Version"));
            addName(names, new Name("Bundle-SymbolicName"));
            addName(names, new Name("Created-By"));
            addName(names, new Name("Export-Package"));
            addName(names, new Name("Import-Package"));
            addName(names, new Name("Name"));
            addName(names, new Name("SHA1-Digest"));
            addName(names, new Name("X-Compile-Source-JDK"));
            addName(names, new Name("X-Compile-Target-JDK"));
            KNOWN_NAMES = names;
        }
    }
}
