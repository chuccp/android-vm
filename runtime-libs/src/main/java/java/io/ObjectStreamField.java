package java.io;

import java.lang.reflect.Field;
import jdk.internal.reflect.CallerSensitive;
import jdk.internal.reflect.Reflection;
import sun.reflect.misc.ReflectUtil;

public class ObjectStreamField
    implements Comparable<Object>
{

    private final String name;

    private final String signature;

    private final Class<?> type;

    private String typeSignature;

    private final boolean unshared;

    private final Field field;

    private int offset;

    public ObjectStreamField(String name, Class<?> type) {
        this(name, type, false);
    }

    public ObjectStreamField(String name, Class<?> type, boolean unshared) {
        if (name == null) {
            throw new NullPointerException();
        }
        this.name = name;
        this.type = type;
        this.unshared = unshared;
        this.field = null;
        this.signature = null;
    }

    ObjectStreamField(String name, String signature, boolean unshared) {
        if (name == null) {
            throw new NullPointerException();
        }
        this.name = name;
        this.signature = signature.intern();
        this.unshared = unshared;
        this.field = null;

        switch (signature.charAt(0)) {
            case 'Z': type = Boolean.TYPE; break;
            case 'B': type = Byte.TYPE; break;
            case 'C': type = Character.TYPE; break;
            case 'S': type = Short.TYPE; break;
            case 'I': type = Integer.TYPE; break;
            case 'J': type = Long.TYPE; break;
            case 'F': type = Float.TYPE; break;
            case 'D': type = Double.TYPE; break;
            case 'L':
            case '[': type = Object.class; break;
            default: throw new IllegalArgumentException("illegal signature");
        }
    }

    private static String getPrimitiveSignature(Class<?> cl) {
        if (cl == Integer.TYPE)
            return "I";
        else if (cl == Byte.TYPE)
            return "B";
        else if (cl == Long.TYPE)
            return "J";
        else if (cl == Float.TYPE)
            return "F";
        else if (cl == Double.TYPE)
            return "D";
        else if (cl == Short.TYPE)
            return "S";
        else if (cl == Character.TYPE)
            return "C";
        else if (cl == Boolean.TYPE)
            return "Z";
        else if (cl == Void.TYPE)
            return "V";
        else
            throw new InternalError();
    }

    static String getClassSignature(Class<?> cl) {
        if (cl.isPrimitive()) {
            return getPrimitiveSignature(cl);
        } else {
            return appendClassSignature(new StringBuilder(), cl).toString();
        }
    }

    static StringBuilder appendClassSignature(StringBuilder sbuf, Class<?> cl) {
        while (cl.isArray()) {
            sbuf.append('[');
            cl = cl.getComponentType();
        }

        if (cl.isPrimitive()) {
            sbuf.append(getPrimitiveSignature(cl));
        } else {
            sbuf.append('L').append(cl.getName().replace('.', '/')).append(';');
        }

        return sbuf;
    }

    ObjectStreamField(Field field, boolean unshared, boolean showType) {
        this.field = field;
        this.unshared = unshared;
        name = field.getName();
        Class<?> ftype = field.getType();
        type = (showType || ftype.isPrimitive()) ? ftype : Object.class;
        signature = getClassSignature(ftype).intern();
    }

    public String getName() {
        return name;
    }

    @CallerSensitive
    public Class<?> getType() {
        if (System.getSecurityManager() != null) {
            Class<?> caller = Reflection.getCallerClass();
            if (ReflectUtil.needsPackageAccessCheck(caller.getClassLoader(), type.getClassLoader())) {
                ReflectUtil.checkPackageAccess(type);
            }
        }
        return type;
    }

    public char getTypeCode() {
        return getSignature().charAt(0);
    }

    public String getTypeString() {
        return isPrimitive() ? null : getSignature();
    }

    public int getOffset() {
        return offset;
    }

    protected void setOffset(int offset) {
        this.offset = offset;
    }

    public boolean isPrimitive() {
        char tcode = getTypeCode();
        return ((tcode != 'L') && (tcode != '['));
    }

    public boolean isUnshared() {
        return unshared;
    }

    public int compareTo(Object obj) {
        ObjectStreamField other = (ObjectStreamField) obj;
        boolean isPrim = isPrimitive();
        if (isPrim != other.isPrimitive()) {
            return isPrim ? -1 : 1;
        }
        return name.compareTo(other.name);
    }

    public String toString() {
        return getSignature() + ' ' + name;
    }

    Field getField() {
        return field;
    }

    String getSignature() {
        if (signature != null) {
            return signature;
        }

        String sig = typeSignature;

        if (sig == null) {
            typeSignature = sig = getClassSignature(type).intern();
        }
        return sig;
    }
}
