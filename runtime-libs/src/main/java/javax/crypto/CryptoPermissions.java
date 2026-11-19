package javax.crypto;

import java.security.*;
import java.util.Enumeration;
import java.util.Hashtable;
import java.util.Vector;
import java.util.NoSuchElementException;
import java.util.concurrent.ConcurrentHashMap;
import java.io.Serializable;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.ObjectStreamField;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.IOException;

final class CryptoPermissions extends PermissionCollection
implements Serializable {

    private static final long serialVersionUID = 4946547168093391015L;

    private static final ObjectStreamField[] serialPersistentFields = {
        new ObjectStreamField("perms", Hashtable.class),
    };

    private transient ConcurrentHashMap<String,PermissionCollection> perms;

    CryptoPermissions() {
        perms = new ConcurrentHashMap<>(7);
    }

    void load(InputStream in)
        throws IOException, CryptoPolicyParser.ParsingException {
        CryptoPolicyParser parser = new CryptoPolicyParser();
        parser.read(new BufferedReader(new InputStreamReader(in, "UTF-8")));

        CryptoPermission[] parsingResult = parser.getPermissions();
        for (int i = 0; i < parsingResult.length; i++) {
            this.add(parsingResult[i]);
        }
    }

    boolean isEmpty() {
        return perms.isEmpty();
    }

    @Override
    public void add(Permission permission) {

        if (isReadOnly()) {
            throw new SecurityException("Attempt to add a Permission " +
                                        "to a readonly CryptoPermissions " +
                                        "object");
        }

        if (!(permission instanceof CryptoPermission)) {
            return;
        }

        CryptoPermission cryptoPerm = (CryptoPermission)permission;
        PermissionCollection pc =
                        getPermissionCollection(cryptoPerm);
        pc.add(cryptoPerm);
        String alg = cryptoPerm.getAlgorithm();
        perms.putIfAbsent(alg, pc);
    }

    @Override
    public boolean implies(Permission permission) {
        if (!(permission instanceof CryptoPermission)) {
            return false;
        }

        CryptoPermission cryptoPerm = (CryptoPermission)permission;

        PermissionCollection pc =
            getPermissionCollection(cryptoPerm.getAlgorithm());

        if (pc != null) {
            return pc.implies(cryptoPerm);
        } else {

            return false;
        }
    }

    @Override
    public Enumeration<Permission> elements() {

        return new PermissionsEnumerator(perms.elements());
    }

    CryptoPermissions getMinimum(CryptoPermissions other) {
        if (other == null) {
            return null;
        }

        if (this.perms.containsKey(CryptoAllPermission.ALG_NAME)) {
            return other;
        }

        if (other.perms.containsKey(CryptoAllPermission.ALG_NAME)) {
            return this;
        }

        CryptoPermissions ret = new CryptoPermissions();

        PermissionCollection thatWildcard =
                other.perms.get(CryptoPermission.ALG_NAME_WILDCARD);
        int maxKeySize = 0;
        if (thatWildcard != null) {
            maxKeySize = ((CryptoPermission)
                    thatWildcard.elements().nextElement()).getMaxKeySize();
        }

        Enumeration<String> thisKeys = this.perms.keys();
        while (thisKeys.hasMoreElements()) {
            String alg = thisKeys.nextElement();

            PermissionCollection thisPc = this.perms.get(alg);
            PermissionCollection thatPc = other.perms.get(alg);

            CryptoPermission[] partialResult;

            if (thatPc == null) {
                if (thatWildcard == null) {

                    continue;
                }
                partialResult = getMinimum(maxKeySize, thisPc);
            } else {
                partialResult = getMinimum(thisPc, thatPc);
            }

            for (int i = 0; i < partialResult.length; i++) {
                ret.add(partialResult[i]);
            }
        }

        PermissionCollection thisWildcard =
                this.perms.get(CryptoPermission.ALG_NAME_WILDCARD);

        if (thisWildcard == null) {
            return ret;
        }

        maxKeySize =
            ((CryptoPermission)
                    thisWildcard.elements().nextElement()).getMaxKeySize();
        Enumeration<String> thatKeys = other.perms.keys();
        while (thatKeys.hasMoreElements()) {
            String alg = thatKeys.nextElement();

            if (this.perms.containsKey(alg)) {
                continue;
            }

            PermissionCollection thatPc = other.perms.get(alg);

            CryptoPermission[] partialResult;

            partialResult = getMinimum(maxKeySize, thatPc);

            for (int i = 0; i < partialResult.length; i++) {
                ret.add(partialResult[i]);
            }
        }
        return ret;
    }

    private CryptoPermission[] getMinimum(PermissionCollection thisPc,
                                          PermissionCollection thatPc) {
        Vector<CryptoPermission> permVector = new Vector<>(2);

        Enumeration<Permission> thisPcPermissions = thisPc.elements();

        while (thisPcPermissions.hasMoreElements()) {
            CryptoPermission thisCp =
                (CryptoPermission)thisPcPermissions.nextElement();

            Enumeration<Permission> thatPcPermissions = thatPc.elements();
            while (thatPcPermissions.hasMoreElements()) {
                CryptoPermission thatCp =
                    (CryptoPermission)thatPcPermissions.nextElement();

                if (thatCp.implies(thisCp)) {
                    permVector.addElement(thisCp);
                    break;
                }
                if (thisCp.implies(thatCp)) {
                    permVector.addElement(thatCp);
                }
            }
        }

        CryptoPermission[] ret = new CryptoPermission[permVector.size()];
        permVector.copyInto(ret);
        return ret;
    }

    private CryptoPermission[] getMinimum(int maxKeySize,
                                          PermissionCollection pc) {
        Vector<CryptoPermission> permVector = new Vector<>(1);

        Enumeration<Permission> enum_ = pc.elements();

        while (enum_.hasMoreElements()) {
            CryptoPermission cp =
                (CryptoPermission)enum_.nextElement();
            if (cp.getMaxKeySize() <= maxKeySize) {
                permVector.addElement(cp);
            } else {
                if (cp.getCheckParam()) {
                    permVector.addElement(
                           new CryptoPermission(cp.getAlgorithm(),
                                                maxKeySize,
                                                cp.getAlgorithmParameterSpec(),
                                                cp.getExemptionMechanism()));
                } else {
                    permVector.addElement(
                           new CryptoPermission(cp.getAlgorithm(),
                                                maxKeySize,
                                                cp.getExemptionMechanism()));
                }
            }
        }

        CryptoPermission[] ret = new CryptoPermission[permVector.size()];
        permVector.copyInto(ret);
        return ret;
    }

    PermissionCollection getPermissionCollection(String alg) {

        PermissionCollection pc = perms.get(CryptoAllPermission.ALG_NAME);
        if (pc == null) {
            pc = perms.get(alg);

            if (pc == null) {
                pc = perms.get(CryptoPermission.ALG_NAME_WILDCARD);
            }
        }
        return pc;
    }

    private PermissionCollection getPermissionCollection(
                                          CryptoPermission cryptoPerm) {

        String alg = cryptoPerm.getAlgorithm();

        PermissionCollection pc = perms.get(alg);

        if (pc == null) {
            pc = cryptoPerm.newPermissionCollection();
        }
        return pc;
    }

    private void readObject(ObjectInputStream s)
        throws IOException, ClassNotFoundException {
        ObjectInputStream.GetField fields = s.readFields();
        @SuppressWarnings("unchecked")
        Hashtable<String,PermissionCollection> permTable =
                (Hashtable<String,PermissionCollection>)
                (fields.get("perms", null));
        if (permTable != null) {
            perms = new ConcurrentHashMap<>(permTable);
        } else {
            perms = new ConcurrentHashMap<>();
        }
    }

    private void writeObject(ObjectOutputStream s) throws IOException {
        Hashtable<String,PermissionCollection> permTable =
                new Hashtable<>(perms);
        ObjectOutputStream.PutField fields = s.putFields();
        fields.put("perms", permTable);
        s.writeFields();
    }
}

final class PermissionsEnumerator implements Enumeration<Permission> {

    private final Enumeration<PermissionCollection> perms;

    private Enumeration<Permission> permset;

    PermissionsEnumerator(Enumeration<PermissionCollection> e) {
        perms = e;
        permset = getNextEnumWithMore();
    }

    @Override
    public synchronized boolean hasMoreElements() {

        if (permset == null) {
            return  false;
        }

        if (permset.hasMoreElements()) {
            return true;
        }

        permset = getNextEnumWithMore();

        return (permset != null);
    }

    @Override
    public synchronized Permission nextElement() {

        if (hasMoreElements()) {
            return permset.nextElement();
        } else {
            throw new NoSuchElementException("PermissionsEnumerator");
        }
    }

    private Enumeration<Permission> getNextEnumWithMore() {
        while (perms.hasMoreElements()) {
            PermissionCollection pc = perms.nextElement();
            Enumeration<Permission> next = pc.elements();
            if (next.hasMoreElements()) {
                return next;
            }
        }
        return null;
    }
}
