package java.security;

import java.io.IOException;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.ObjectStreamField;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;

final class UnresolvedPermissionCollection
extends PermissionCollection
implements java.io.Serializable
{

    private transient ConcurrentHashMap<String, List<UnresolvedPermission>> perms;

    public UnresolvedPermissionCollection() {
        perms = new ConcurrentHashMap<>(11);
    }

    @Override
    public void add(Permission permission) {
        if (! (permission instanceof UnresolvedPermission))
            throw new IllegalArgumentException("invalid permission: "+
                                               permission);
        UnresolvedPermission up = (UnresolvedPermission) permission;

        perms.compute(up.getName(),
            new java.util.function.BiFunction<>() {
                @Override
                public List<UnresolvedPermission> apply(String key,
                                        List<UnresolvedPermission> oldValue) {
                    if (oldValue == null) {
                        List<UnresolvedPermission> v =
                            new CopyOnWriteArrayList<>();
                        v.add(up);
                        return v;
                    } else {
                        oldValue.add(up);
                        return oldValue;
                    }
                }
            }
        );
    }

    List<UnresolvedPermission> getUnresolvedPermissions(Permission p) {
        return perms.get(p.getClass().getName());
    }

    @Override
    public boolean implies(Permission permission) {
        return false;
    }

    @Override
    public Enumeration<Permission> elements() {
        List<Permission> results =
            new ArrayList<>();

        for (List<UnresolvedPermission> l : perms.values()) {
            results.addAll(l);
        }

        return Collections.enumeration(results);
    }

    private static final long serialVersionUID = -7176153071733132400L;

    private static final ObjectStreamField[] serialPersistentFields = {
        new ObjectStreamField("permissions", Hashtable.class),
    };

    private void writeObject(ObjectOutputStream out) throws IOException {

        Hashtable<String, Vector<UnresolvedPermission>> permissions =
            new Hashtable<>(perms.size()*2);

        Set<Map.Entry<String, List<UnresolvedPermission>>> set = perms.entrySet();
        for (Map.Entry<String, List<UnresolvedPermission>> e : set) {

            List<UnresolvedPermission> list = e.getValue();
            Vector<UnresolvedPermission> vec = new Vector<>(list);

            permissions.put(e.getKey(), vec);
        }

        ObjectOutputStream.PutField pfields = out.putFields();
        pfields.put("permissions", permissions);
        out.writeFields();
    }

    private void readObject(ObjectInputStream in) throws IOException,
    ClassNotFoundException {

        ObjectInputStream.GetField gfields = in.readFields();

        @SuppressWarnings("unchecked")

        Hashtable<String, Vector<UnresolvedPermission>> permissions =
                (Hashtable<String, Vector<UnresolvedPermission>>)
                gfields.get("permissions", null);
        perms = new ConcurrentHashMap<>(permissions.size()*2);

        Set<Map.Entry<String, Vector<UnresolvedPermission>>> set = permissions.entrySet();
        for (Map.Entry<String, Vector<UnresolvedPermission>> e : set) {

            Vector<UnresolvedPermission> vec = e.getValue();
            List<UnresolvedPermission> list = new CopyOnWriteArrayList<>(vec);

            perms.put(e.getKey(), list);
        }
    }
}
