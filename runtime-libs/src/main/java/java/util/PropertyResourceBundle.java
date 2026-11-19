package java.util;

import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.io.IOException;
import java.nio.charset.MalformedInputException;
import java.nio.charset.UnmappableCharacterException;
import sun.nio.cs.ISO_8859_1;
import sun.security.action.GetPropertyAction;
import sun.util.PropertyResourceBundleCharset;
import sun.util.ResourceBundleEnumeration;

public class PropertyResourceBundle extends ResourceBundle {

    private static final String encoding = GetPropertyAction
        .privilegedGetProperty("java.util.PropertyResourceBundle.encoding", "")
        .toUpperCase(Locale.ROOT);

    @SuppressWarnings({"unchecked", "rawtypes"})
    public PropertyResourceBundle (InputStream stream) throws IOException {
        this(new InputStreamReader(stream,
            "ISO-8859-1".equals(encoding) ?
                ISO_8859_1.INSTANCE.newDecoder() :
                new PropertyResourceBundleCharset("UTF-8".equals(encoding)).newDecoder()));
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    public PropertyResourceBundle (Reader reader) throws IOException {
        Properties properties = new Properties();
        properties.load(reader);
        lookup = new HashMap(properties);
    }

    public Object handleGetObject(String key) {
        if (key == null) {
            throw new NullPointerException();
        }
        return lookup.get(key);
    }

    public Enumeration<String> getKeys() {
        ResourceBundle parent = this.parent;
        return new ResourceBundleEnumeration(lookup.keySet(),
                (parent != null) ? parent.getKeys() : null);
    }

    protected Set<String> handleKeySet() {
        return lookup.keySet();
    }

    private final Map<String,Object> lookup;
}
