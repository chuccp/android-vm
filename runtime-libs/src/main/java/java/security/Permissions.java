package java.security;

import java.util.Enumeration;
import java.util.Hashtable;
import java.util.NoSuchElementException;
import java.util.Map;
import java.util.HashMap;
import java.util.List;
import java.util.Iterator;
import java.util.Collections;
import java.util.concurrent.ConcurrentHashMap;
import java.io.Serializable;
import java.io.ObjectStreamField;
import java.io.ObjectOutputStream;
import java.io.ObjectInputStream;
import java.io.IOException;

public final class Permissions extends PermissionCollection
implements Serializable
{

    private transient ConcurrentHashMap<Class<?>, PermissionCollection> permsMap;

    private transient boolean hasUnresolved = false;

    PermissionCollection allPermission;

    public Permissions() {
        permsMap = new ConcurrentHashMap<>(11);
        allPermission = null;
    }

    @Override
    public void add(Permission permission) {
        if (isReadOnly())
            throw new SecurityException(
              "attempt to add a Permission to a readonly Permissions object");

        PermissionCollection pc = getPermissionCollection(permission, true);
        pc.add(permission);

        if (permission instanceof AllPermission) {
            allPermission = pc;
        }
        if (permission instanceof UnresolvedPermission) {
            hasUnresolved = true;
        }
    }

    @Override
    public boolean implies(Permission permission) {

        if (allPermission != null) {
            return true;
        } else {
            PermissionCollection pc = getPermissionCollection(permission,
                false);
            if (pc != null) {
                return pc.implies(permission);
            } else {

                return false;
            }
        }
    }

    @Override
    public Enumeration<Permission> elements() {

        return new PermissionsEnumerator(permsMap.values().iterator());
    }

    private PermissionCollection getPermissionCollection(Permission p,
                                                         boolean createEmpty) {
        Class<?> c = p.getClass();

        if (!hasUnresolved && !createEmpty) {
            return permsMap.get(c);
        }

        return permsMap.computeIfAbsent(c,
            new java.util.function.Function<>() {
                @Override
                public PermissionCollection apply(Class<?> k) {

                    PermissionCollection pc =
                        (hasUnresolved ? getUnresolvedPermissions(p) : null);

                    if (pc == null && createEmpty) {

                        pc = p.newPermissionCollection();

                        if (pc == null) {
                            pc = new PermissionsHash();
                        }
                    }
                    return pc;
                }
            }
        );
    }

    private PermissionCollection getUnresolvedPermissions(Permission p)
    {
        UnresolvedPermissionCollection uc =
        (UnresolvedPermissionCollection) permsMap.get(UnresolvedPermission.class);

        if (uc == null)
            return null;

        List<UnresolvedPermission> unresolvedPerms =
                                        uc.getUnresolvedPermissions(p);

        if (unresolvedPerms == null)
            return null;

        java.security.cert.Certificate[] certs = null;

        Object[] signers = p.getClass().getSigners();

        int n = 0;
        if (signers != null) {
            for (int j=0; j < signers.length; j++) {
                if (signers[j] instanceof java.security.cert.Certificate) {
                    n++;
                }
            }
            certs = new java.security.cert.Certificate[n];
            n = 0;
            for (int j=0; j < signers.length; j++) {
                if (signers[j] instanceof java.security.cert.Certificate) {
                    certs[n++] = (java.security.cert.Certificate)signers[j];
                }
            }
        }

        PermissionCollection pc = null;
        synchronized (unresolvedPerms) {
            int len = unresolvedPerms.size();
            for (int i = 0; i < len; i++) {
                UnresolvedPermission up = unresolvedPerms.get(i);
                Permission perm = up.resolve(p, certs);
                if (perm != null) {
                    if (pc == null) {
                        pc = p.newPermissionCollection();
                        if (pc == null)
                            pc = new PermissionsHash();
                    }
                    pc.add(perm);
                }
            }
        }
        return pc;
    }

    private static final long serialVersionUID = 4858622370623524688L;

    private static final ObjectStreamField[] serialPersistentFields = {
        new ObjectStreamField("perms", Hashtable.class),
        new ObjectStreamField("allPermission", PermissionCollection.class),
    };

    private void writeObject(ObjectOutputStream out) throws IOException {

        Hashtable<Class<?>, PermissionCollection> perms =
            new Hashtable<>(permsMap.size()*2);
        perms.putAll(permsMap);

        ObjectOutputStream.PutField pfields = out.putFields();

        pfields.put("allPermission", allPermission);
        pfields.put("perms", perms);
        out.writeFields();
    }

    private void readObject(ObjectInputStream in) throws IOException,
    ClassNotFoundException {

        ObjectInputStream.GetField gfields = in.readFields();

        allPermission = (PermissionCollection) gfields.get("allPermission", null);

        @SuppressWarnings("unchecked")
        Hashtable<Class<?>, PermissionCollection> perms =
            (Hashtable<Class<?>, PermissionCollection>)gfields.get("perms", null);
        permsMap = new ConcurrentHashMap<>(perms.size()*2);
        permsMap.putAll(perms);

        UnresolvedPermissionCollection uc =
        (UnresolvedPermissionCollection) permsMap.get(UnresolvedPermission.class);
        hasUnresolved = (uc != null && uc.elements().hasMoreElements());
    }
}

final class PermissionsEnumerator implements Enumeration<Permission> {

    private Iterator<PermissionCollection> perms;

    private Enumeration<Permission> permset;

    PermissionsEnumerator(Iterator<PermissionCollection> e) {
        perms = e;
        permset = getNextEnumWithMore();
    }

    public boolean hasMoreElements() {

        if (permset == null)
            return  false;

        if (permset.hasMoreElements())
            return true;

        permset = getNextEnumWithMore();

        return (permset != null);
    }

    public Permission nextElement() {

        if (hasMoreElements()) {
            return permset.nextElement();
        } else {
            throw new NoSuchElementException("PermissionsEnumerator");
        }

    }

    private Enumeration<Permission> getNextEnumWithMore() {
        while (perms.hasNext()) {
            PermissionCollection pc = perms.next();
            Enumeration<Permission> next =pc.elements();
            if (next.hasMoreElements())
                return next;
        }
        return null;

    }
}

final class PermissionsHash extends PermissionCollection
implements Serializable
{

    private transient ConcurrentHashMap<Permission, Permission> permsMap;

    PermissionsHash() {
        permsMap = new ConcurrentHashMap<>(11);
    }

    @Override
    public void add(Permission permission) {
        permsMap.put(permission, permission);
    }

    @Override
    public boolean implies(Permission permission) {

        Permission p = permsMap.get(permission);

        if (p == null) {
            for (Permission p_ : permsMap.values()) {
                if (p_.implies(permission))
                    return true;
            }
            return false;
        } else {
            return true;
        }
    }

    @Override
    public Enumeration<Permission> elements() {
        return permsMap.elements();
    }

    private static final long serialVersionUID = -8491988220802933440L;

    private static final ObjectStreamField[] serialPersistentFields = {
        new ObjectStreamField("perms", Hashtable.class),
    };

    private void writeObject(ObjectOutputStream out) throws IOException {

        Hashtable<Permission, Permission> perms =
                new Hashtable<>(permsMap.size()*2);
        perms.putAll(permsMap);

        ObjectOutputStream.PutField pfields = out.putFields();
        pfields.put("perms", perms);
        out.writeFields();
    }

    private void readObject(ObjectInputStream in) throws IOException,
    ClassNotFoundException {

        ObjectInputStream.GetField gfields = in.readFields();

        @SuppressWarnings("unchecked")
        Hashtable<Permission, Permission> perms =
                (Hashtable<Permission, Permission>)gfields.get("perms", null);
        permsMap = new ConcurrentHashMap<>(perms.size()*2);
        permsMap.putAll(perms);
    }
}
