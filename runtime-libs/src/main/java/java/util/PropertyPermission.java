package java.util;

import java.io.IOException;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.ObjectStreamField;
import java.io.Serializable;
import java.security.*;
import java.util.Enumeration;
import java.util.Hashtable;
import java.util.concurrent.ConcurrentHashMap;
import sun.security.util.SecurityConstants;

public final class PropertyPermission extends BasicPermission {

    private static final int READ    = 0x1;

    private static final int WRITE   = 0x2;

    private static final int ALL     = READ|WRITE;

    private static final int NONE    = 0x0;

    private transient int mask;

    private String actions;

    private void init(int mask) {
        if ((mask & ALL) != mask)
            throw new IllegalArgumentException("invalid actions mask");

        if (mask == NONE)
            throw new IllegalArgumentException("invalid actions mask");

        if (getName() == null)
            throw new NullPointerException("name can't be null");

        this.mask = mask;
    }

    public PropertyPermission(String name, String actions) {
        super(name,actions);
        init(getMask(actions));
    }

    PropertyPermission(String name, int mask) {
        super(name, getActions(mask));
        this.mask = mask;
    }

    @Override
    public boolean implies(Permission p) {
        if (!(p instanceof PropertyPermission))
            return false;

        PropertyPermission that = (PropertyPermission) p;

        return ((this.mask & that.mask) == that.mask) && super.implies(that);
    }

    @Override
    public boolean equals(Object obj) {
        if (obj == this)
            return true;

        if (! (obj instanceof PropertyPermission))
            return false;

        PropertyPermission that = (PropertyPermission) obj;

        return (this.mask == that.mask) &&
            (this.getName().equals(that.getName()));
    }

    @Override
    public int hashCode() {
        return this.getName().hashCode();
    }

    private static int getMask(String actions) {

        int mask = NONE;

        if (actions == null) {
            return mask;
        }

        if (actions == SecurityConstants.PROPERTY_READ_ACTION) {
            return READ;
        } if (actions == SecurityConstants.PROPERTY_WRITE_ACTION) {
            return WRITE;
        } else if (actions == SecurityConstants.PROPERTY_RW_ACTION) {
            return READ|WRITE;
        }

        char[] a = actions.toCharArray();

        int i = a.length - 1;
        if (i < 0)
            return mask;

        while (i != -1) {
            char c;

            while ((i!=-1) && ((c = a[i]) == ' ' ||
                               c == '\r' ||
                               c == '\n' ||
                               c == '\f' ||
                               c == '\t'))
                i--;

            int matchlen;

            if (i >= 3 && (a[i-3] == 'r' || a[i-3] == 'R') &&
                          (a[i-2] == 'e' || a[i-2] == 'E') &&
                          (a[i-1] == 'a' || a[i-1] == 'A') &&
                          (a[i] == 'd' || a[i] == 'D'))
            {
                matchlen = 4;
                mask |= READ;

            } else if (i >= 4 && (a[i-4] == 'w' || a[i-4] == 'W') &&
                                 (a[i-3] == 'r' || a[i-3] == 'R') &&
                                 (a[i-2] == 'i' || a[i-2] == 'I') &&
                                 (a[i-1] == 't' || a[i-1] == 'T') &&
                                 (a[i] == 'e' || a[i] == 'E'))
            {
                matchlen = 5;
                mask |= WRITE;

            } else {

                throw new IllegalArgumentException(
                        "invalid permission: " + actions);
            }

            boolean seencomma = false;
            while (i >= matchlen && !seencomma) {
                switch(a[i-matchlen]) {
                case ',':
                    seencomma = true;
                    break;
                case ' ': case '\r': case '\n':
                case '\f': case '\t':
                    break;
                default:
                    throw new IllegalArgumentException(
                            "invalid permission: " + actions);
                }
                i--;
            }

            i -= matchlen;
        }

        return mask;
    }

    static String getActions(int mask) {
        switch (mask & (READ|WRITE)) {
            case READ:
                return SecurityConstants.PROPERTY_READ_ACTION;
            case WRITE:
                return SecurityConstants.PROPERTY_WRITE_ACTION;
            case READ|WRITE:
                return SecurityConstants.PROPERTY_RW_ACTION;
            default:
                return "";
        }
    }

    @Override
    public String getActions() {
        if (actions == null)
            actions = getActions(this.mask);

        return actions;
    }

    int getMask() {
        return mask;
    }

    @Override
    public PermissionCollection newPermissionCollection() {
        return new PropertyPermissionCollection();
    }

    private static final long serialVersionUID = 885438825399942851L;

    private synchronized void writeObject(java.io.ObjectOutputStream s)
        throws IOException
    {

        if (actions == null)
            getActions();
        s.defaultWriteObject();
    }

    private synchronized void readObject(java.io.ObjectInputStream s)
         throws IOException, ClassNotFoundException
    {

        s.defaultReadObject();
        init(getMask(actions));
    }
}

final class PropertyPermissionCollection extends PermissionCollection
    implements Serializable
{

    private transient ConcurrentHashMap<String, PropertyPermission> perms;

    private boolean all_allowed;

    public PropertyPermissionCollection() {
        perms = new ConcurrentHashMap<>(32);
        all_allowed = false;
    }

    @Override
    public void add(Permission permission) {
        if (! (permission instanceof PropertyPermission))
            throw new IllegalArgumentException("invalid permission: "+
                                               permission);
        if (isReadOnly())
            throw new SecurityException(
                "attempt to add a Permission to a readonly PermissionCollection");

        PropertyPermission pp = (PropertyPermission) permission;
        String propName = pp.getName();

        perms.merge(propName, pp,
            new java.util.function.BiFunction<>() {
                @Override
                public PropertyPermission apply(PropertyPermission existingVal,
                                                PropertyPermission newVal) {

                    int oldMask = existingVal.getMask();
                    int newMask = newVal.getMask();
                    if (oldMask != newMask) {
                        int effective = oldMask | newMask;
                        if (effective == newMask) {
                            return newVal;
                        }
                        if (effective != oldMask) {
                            return new PropertyPermission(propName, effective);
                        }
                    }
                    return existingVal;
                }
            }
        );

        if (!all_allowed) {
            if (propName.equals("*"))
                all_allowed = true;
        }
    }

    @Override
    public boolean implies(Permission permission) {
        if (! (permission instanceof PropertyPermission))
            return false;

        PropertyPermission pp = (PropertyPermission) permission;
        PropertyPermission x;

        int desired = pp.getMask();
        int effective = 0;

        if (all_allowed) {
            x = perms.get("*");
            if (x != null) {
                effective |= x.getMask();
                if ((effective & desired) == desired)
                    return true;
            }
        }

        String name = pp.getName();

        x = perms.get(name);

        if (x != null) {

            effective |= x.getMask();
            if ((effective & desired) == desired)
                return true;
        }

        int last, offset;

        offset = name.length()-1;

        while ((last = name.lastIndexOf('.', offset)) != -1) {

            name = name.substring(0, last+1) + "*";

            x = perms.get(name);

            if (x != null) {
                effective |= x.getMask();
                if ((effective & desired) == desired)
                    return true;
            }
            offset = last -1;
        }

        return false;
    }

    @Override
    @SuppressWarnings("unchecked")
    public Enumeration<Permission> elements() {

        return (Enumeration)perms.elements();
    }

    private static final long serialVersionUID = 7015263904581634791L;

    private static final ObjectStreamField[] serialPersistentFields = {
        new ObjectStreamField("permissions", Hashtable.class),
        new ObjectStreamField("all_allowed", Boolean.TYPE),
    };

    private void writeObject(ObjectOutputStream out) throws IOException {

        Hashtable<String, Permission> permissions =
            new Hashtable<>(perms.size()*2);
        permissions.putAll(perms);

        ObjectOutputStream.PutField pfields = out.putFields();
        pfields.put("all_allowed", all_allowed);
        pfields.put("permissions", permissions);
        out.writeFields();
    }

    private void readObject(ObjectInputStream in)
        throws IOException, ClassNotFoundException
    {

        ObjectInputStream.GetField gfields = in.readFields();

        all_allowed = gfields.get("all_allowed", false);

        @SuppressWarnings("unchecked")
        Hashtable<String, PropertyPermission> permissions =
            (Hashtable<String, PropertyPermission>)gfields.get("permissions", null);
        perms = new ConcurrentHashMap<>(permissions.size()*2);
        perms.putAll(permissions);
    }
}
