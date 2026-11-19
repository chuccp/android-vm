package java.io;

import java.nio.file.*;
import java.security.*;
import java.util.Enumeration;
import java.util.Objects;
import java.util.StringJoiner;
import java.util.Vector;
import java.util.concurrent.ConcurrentHashMap;

import jdk.internal.misc.JavaIOFilePermissionAccess;
import jdk.internal.misc.SharedSecrets;
import sun.nio.fs.DefaultFileSystemProvider;
import sun.security.action.GetPropertyAction;
import sun.security.util.FilePermCompat;
import sun.security.util.SecurityConstants;

public final class FilePermission extends Permission implements Serializable {

    private static final int EXECUTE = 0x1;

    private static final int WRITE   = 0x2;

    private static final int READ    = 0x4;

    private static final int DELETE  = 0x8;

    private static final int READLINK    = 0x10;

    private static final int ALL     = READ|WRITE|EXECUTE|DELETE|READLINK;

    private static final int NONE    = 0x0;

    private transient int mask;

    private transient boolean directory;

    private transient boolean recursive;

    private String actions;

    private transient String cpath;

    private transient Path npath;
    private transient Path npath2;
    private transient boolean allFiles;
    private transient boolean invalid;

    private static final char RECURSIVE_CHAR = '-';
    private static final char WILD_CHAR = '*';

    private static final long serialVersionUID = 7930732926638008763L;

    private static final java.nio.file.FileSystem builtInFS =
        DefaultFileSystemProvider.theFileSystem();

    private static final Path here = builtInFS.getPath(
            GetPropertyAction.privilegedGetProperty("user.dir"));

    private static final Path EMPTY_PATH = builtInFS.getPath("");
    private static final Path DASH_PATH = builtInFS.getPath("-");
    private static final Path DOTDOT_PATH = builtInFS.getPath("..");

    private FilePermission(String name,
                           FilePermission input,
                           Path npath,
                           Path npath2,
                           int mask,
                           String actions) {
        super(name);

        this.npath = npath;
        this.npath2 = npath2;
        this.actions = actions;
        this.mask = mask;

        this.allFiles = input.allFiles;
        this.invalid = input.invalid;
        this.recursive = input.recursive;
        this.directory = input.directory;
        this.cpath = input.cpath;
    }

    private static Path altPath(Path in) {
        try {
            if (!in.isAbsolute()) {
                return here.resolve(in).normalize();
            } else {
                return here.relativize(in).normalize();
            }
        } catch (IllegalArgumentException e) {
            return null;
        }
    }

    static {
        SharedSecrets.setJavaIOFilePermissionAccess(

            new JavaIOFilePermissionAccess() {
                public FilePermission newPermPlusAltPath(FilePermission input) {
                    if (!input.invalid && input.npath2 == null && !input.allFiles) {
                        Path npath2 = altPath(input.npath);
                        if (npath2 != null) {

                            return new FilePermission(input.getName() + "#plus",
                                    input,
                                    input.npath,
                                    npath2,
                                    input.mask,
                                    input.actions);
                        }
                    }
                    return input;
                }
                public FilePermission newPermUsingAltPath(FilePermission input) {
                    if (!input.invalid && !input.allFiles) {
                        Path npath2 = altPath(input.npath);
                        if (npath2 != null) {

                            return new FilePermission(input.getName() + "#using",
                                    input,
                                    npath2,
                                    null,
                                    input.mask,
                                    input.actions);
                        }
                    }
                    return null;
                }
            }
        );
    }

    private void init(int mask) {
        if ((mask & ALL) != mask)
                throw new IllegalArgumentException("invalid actions mask");

        if (mask == NONE)
                throw new IllegalArgumentException("invalid actions mask");

        if (FilePermCompat.nb) {
            String name = getName();

            if (name == null)
                throw new NullPointerException("name can't be null");

            this.mask = mask;

            if (name.equals("<<ALL FILES>>")) {
                allFiles = true;
                npath = EMPTY_PATH;

                return;
            }

            boolean rememberStar = false;
            if (name.endsWith("*")) {
                rememberStar = true;
                recursive = false;
                name = name.substring(0, name.length()-1) + "-";
            }

            try {

                npath = builtInFS.getPath(new File(name).getPath())
                        .normalize();

                Path lastName = npath.getFileName();
                if (lastName != null && lastName.equals(DASH_PATH)) {
                    directory = true;
                    recursive = !rememberStar;
                    npath = npath.getParent();
                }
                if (npath == null) {
                    npath = EMPTY_PATH;
                }
                invalid = false;
            } catch (InvalidPathException ipe) {

                npath = builtInFS.getPath("-u-s-e-l-e-s-s-");
                invalid = true;
            }

        } else {
            if ((cpath = getName()) == null)
                throw new NullPointerException("name can't be null");

            this.mask = mask;

            if (cpath.equals("<<ALL FILES>>")) {
                allFiles = true;
                directory = true;
                recursive = true;
                cpath = "";
                return;
            }

            try {
                String name = cpath.endsWith("*") ? cpath.substring(0, cpath.length() - 1) + "-" : cpath;
                builtInFS.getPath(new File(name).getPath());
            } catch (InvalidPathException ipe) {
                invalid = true;
                return;
            }

            cpath = AccessController.doPrivileged(new PrivilegedAction<>() {
                public String run() {
                    try {
                        String path = cpath;
                        if (cpath.endsWith("*")) {

                            path = path.substring(0, path.length() - 1) + "-";
                            path = new File(path).getCanonicalPath();
                            return path.substring(0, path.length() - 1) + "*";
                        } else {
                            return new File(path).getCanonicalPath();
                        }
                    } catch (IOException ioe) {
                        return cpath;
                    }
                }
            });

            int len = cpath.length();
            char last = ((len > 0) ? cpath.charAt(len - 1) : 0);

            if (last == RECURSIVE_CHAR &&
                    cpath.charAt(len - 2) == File.separatorChar) {
                directory = true;
                recursive = true;
                cpath = cpath.substring(0, --len);
            } else if (last == WILD_CHAR &&
                    cpath.charAt(len - 2) == File.separatorChar) {
                directory = true;

                cpath = cpath.substring(0, --len);
            } else {

            }

        }
    }

    public FilePermission(String path, String actions) {
        super(path);
        init(getMask(actions));
    }

    FilePermission(String path, int mask) {
        super(path);
        init(mask);
    }

    @Override
    public boolean implies(Permission p) {
        if (!(p instanceof FilePermission))
            return false;

        FilePermission that = (FilePermission) p;

        return ((this.mask & that.mask) == that.mask) && impliesIgnoreMask(that);
    }

    boolean impliesIgnoreMask(FilePermission that) {
        if (this == that) {
            return true;
        }
        if (allFiles) {
            return true;
        }
        if (this.invalid || that.invalid) {
            return false;
        }
        if (that.allFiles) {
            return false;
        }
        if (FilePermCompat.nb) {

            if ((this.recursive && that.recursive) != that.recursive
                    || (this.directory && that.directory) != that.directory) {
                return false;
            }

            if (this.npath.equals(that.npath)
                    && this.directory == that.directory) {
                return true;
            }
            int diff = containsPath(this.npath, that.npath);

            if (diff >= 1 && recursive) {
                return true;
            }

            if (diff == 1 && directory && !that.directory) {
                return true;
            }

            if (this.npath2 != null) {
                if (this.npath2.equals(that.npath)
                        && this.directory == that.directory) {
                    return true;
                }
                diff = containsPath(this.npath2, that.npath);
                if (diff >= 1 && recursive) {
                    return true;
                }
                if (diff == 1 && directory && !that.directory) {
                    return true;
                }
            }

            return false;
        } else {
            if (this.directory) {
                if (this.recursive) {

                    if (that.directory) {
                        return (that.cpath.length() >= this.cpath.length()) &&
                                that.cpath.startsWith(this.cpath);
                    } else {
                        return ((that.cpath.length() > this.cpath.length()) &&
                                that.cpath.startsWith(this.cpath));
                    }
                } else {
                    if (that.directory) {

                        if (that.recursive)
                            return false;
                        else
                            return (this.cpath.equals(that.cpath));
                    } else {
                        int last = that.cpath.lastIndexOf(File.separatorChar);
                        if (last == -1)
                            return false;
                        else {

                            return (this.cpath.length() == (last + 1)) &&
                                    this.cpath.regionMatches(0, that.cpath, 0, last + 1);
                        }
                    }
                }
            } else if (that.directory) {

                return false;
            } else {
                return (this.cpath.equals(that.cpath));
            }
        }
    }

    private static int containsPath(Path p1, Path p2) {

        if (!Objects.equals(p1.getRoot(), p2.getRoot())) {
            return -1;
        }

        if (p1.equals(EMPTY_PATH)) {
            if (p2.equals(EMPTY_PATH)) {
                return 0;
            } else if (p2.getName(0).equals(DOTDOT_PATH)) {

                return -1;
            } else {

                return p2.getNameCount();
            }
        } else if (p2.equals(EMPTY_PATH)) {
            int c1 = p1.getNameCount();
            if (!p1.getName(c1 - 1).equals(DOTDOT_PATH)) {

                return -1;
            }

            return c1;
        }

        int c1 = p1.getNameCount();
        int c2 = p2.getNameCount();

        int n = Math.min(c1, c2);
        int i = 0;
        while (i < n) {
            if (!p1.getName(i).equals(p2.getName(i)))
                break;
            i++;
        }

        if (i < c1 && !p1.getName(c1 - 1).equals(DOTDOT_PATH)) {
            return -1;
        }

        if (i < c2 && p2.getName(i).equals(DOTDOT_PATH)) {
            return -1;
        }

        return c1 - i + c2 - i;
    }

    @Override
    public boolean equals(Object obj) {
        if (obj == this)
            return true;

        if (! (obj instanceof FilePermission))
            return false;

        FilePermission that = (FilePermission) obj;

        if (this.invalid || that.invalid) {
            return false;
        }
        if (FilePermCompat.nb) {
            return (this.mask == that.mask) &&
                    (this.allFiles == that.allFiles) &&
                    this.npath.equals(that.npath) &&
                    Objects.equals(npath2, that.npath2) &&
                    (this.directory == that.directory) &&
                    (this.recursive == that.recursive);
        } else {
            return (this.mask == that.mask) &&
                    (this.allFiles == that.allFiles) &&
                    this.cpath.equals(that.cpath) &&
                    (this.directory == that.directory) &&
                    (this.recursive == that.recursive);
        }
    }

    @Override
    public int hashCode() {
        if (FilePermCompat.nb) {
            return Objects.hash(
                    mask, allFiles, directory, recursive, npath, npath2, invalid);
        } else {
            return 0;
        }
    }

    private static int getMask(String actions) {
        int mask = NONE;

        if (actions == null) {
            return mask;
        }

        if (actions == SecurityConstants.FILE_READ_ACTION) {
            return READ;
        } else if (actions == SecurityConstants.FILE_WRITE_ACTION) {
            return WRITE;
        } else if (actions == SecurityConstants.FILE_EXECUTE_ACTION) {
            return EXECUTE;
        } else if (actions == SecurityConstants.FILE_DELETE_ACTION) {
            return DELETE;
        } else if (actions == SecurityConstants.FILE_READLINK_ACTION) {
            return READLINK;
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

            } else if (i >= 6 && (a[i-6] == 'e' || a[i-6] == 'E') &&
                                 (a[i-5] == 'x' || a[i-5] == 'X') &&
                                 (a[i-4] == 'e' || a[i-4] == 'E') &&
                                 (a[i-3] == 'c' || a[i-3] == 'C') &&
                                 (a[i-2] == 'u' || a[i-2] == 'U') &&
                                 (a[i-1] == 't' || a[i-1] == 'T') &&
                                 (a[i] == 'e' || a[i] == 'E'))
            {
                matchlen = 7;
                mask |= EXECUTE;

            } else if (i >= 5 && (a[i-5] == 'd' || a[i-5] == 'D') &&
                                 (a[i-4] == 'e' || a[i-4] == 'E') &&
                                 (a[i-3] == 'l' || a[i-3] == 'L') &&
                                 (a[i-2] == 'e' || a[i-2] == 'E') &&
                                 (a[i-1] == 't' || a[i-1] == 'T') &&
                                 (a[i] == 'e' || a[i] == 'E'))
            {
                matchlen = 6;
                mask |= DELETE;

            } else if (i >= 7 && (a[i-7] == 'r' || a[i-7] == 'R') &&
                                 (a[i-6] == 'e' || a[i-6] == 'E') &&
                                 (a[i-5] == 'a' || a[i-5] == 'A') &&
                                 (a[i-4] == 'd' || a[i-4] == 'D') &&
                                 (a[i-3] == 'l' || a[i-3] == 'L') &&
                                 (a[i-2] == 'i' || a[i-2] == 'I') &&
                                 (a[i-1] == 'n' || a[i-1] == 'N') &&
                                 (a[i] == 'k' || a[i] == 'K'))
            {
                matchlen = 8;
                mask |= READLINK;

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

    int getMask() {
        return mask;
    }

    private static String getActions(int mask) {
        StringJoiner sj = new StringJoiner(",");

        if ((mask & READ) == READ) {
            sj.add("read");
        }
        if ((mask & WRITE) == WRITE) {
            sj.add("write");
        }
        if ((mask & EXECUTE) == EXECUTE) {
            sj.add("execute");
        }
        if ((mask & DELETE) == DELETE) {
            sj.add("delete");
        }
        if ((mask & READLINK) == READLINK) {
            sj.add("readlink");
        }

        return sj.toString();
    }

    @Override
    public String getActions() {
        if (actions == null)
            actions = getActions(this.mask);

        return actions;
    }

    @Override
    public PermissionCollection newPermissionCollection() {
        return new FilePermissionCollection();
    }

    private void writeObject(ObjectOutputStream s)
        throws IOException
    {

        if (actions == null)
            getActions();
        s.defaultWriteObject();
    }

    private void readObject(ObjectInputStream s)
         throws IOException, ClassNotFoundException
    {

        s.defaultReadObject();
        init(getMask(actions));
    }

    FilePermission withNewActions(int effective) {
        return new FilePermission(this.getName(),
                this,
                this.npath,
                this.npath2,
                effective,
                null);
    }
}

final class FilePermissionCollection extends PermissionCollection
    implements Serializable
{

    private transient ConcurrentHashMap<String, Permission> perms;

    public FilePermissionCollection() {
        perms = new ConcurrentHashMap<>();
    }

    @Override
    public void add(Permission permission) {
        if (! (permission instanceof FilePermission))
            throw new IllegalArgumentException("invalid permission: "+
                                               permission);
        if (isReadOnly())
            throw new SecurityException(
                "attempt to add a Permission to a readonly PermissionCollection");

        FilePermission fp = (FilePermission)permission;

        perms.merge(fp.getName(), fp,
            new java.util.function.BiFunction<>() {
                @Override
                public Permission apply(Permission existingVal,
                                        Permission newVal) {
                    int oldMask = ((FilePermission)existingVal).getMask();
                    int newMask = ((FilePermission)newVal).getMask();
                    if (oldMask != newMask) {
                        int effective = oldMask | newMask;
                        if (effective == newMask) {
                            return newVal;
                        }
                        if (effective != oldMask) {
                            return ((FilePermission)newVal)
                                    .withNewActions(effective);
                        }
                    }
                    return existingVal;
                }
            }
        );
    }

    @Override
    public boolean implies(Permission permission) {
        if (! (permission instanceof FilePermission))
            return false;

        FilePermission fperm = (FilePermission) permission;

        int desired = fperm.getMask();
        int effective = 0;
        int needed = desired;

        for (Permission perm : perms.values()) {
            FilePermission fp = (FilePermission)perm;
            if (((needed & fp.getMask()) != 0) && fp.impliesIgnoreMask(fperm)) {
                effective |= fp.getMask();
                if ((effective & desired) == desired) {
                    return true;
                }
                needed = (desired ^ effective);
            }
        }
        return false;
    }

    @Override
    public Enumeration<Permission> elements() {
        return perms.elements();
    }

    private static final long serialVersionUID = 2202956749081564585L;

    private static final ObjectStreamField[] serialPersistentFields = {
        new ObjectStreamField("permissions", Vector.class),
    };

    private void writeObject(ObjectOutputStream out) throws IOException {

        Vector<Permission> permissions = new Vector<>(perms.values());

        ObjectOutputStream.PutField pfields = out.putFields();
        pfields.put("permissions", permissions);
        out.writeFields();
    }

    private void readObject(ObjectInputStream in)
        throws IOException, ClassNotFoundException
    {

        ObjectInputStream.GetField gfields = in.readFields();

        @SuppressWarnings("unchecked")
        Vector<Permission> permissions = (Vector<Permission>)gfields.get("permissions", null);
        perms = new ConcurrentHashMap<>(permissions.size());
        for (Permission perm : permissions) {
            perms.put(perm.getName(), perm);
        }
    }
}
