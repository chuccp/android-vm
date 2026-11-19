package java.io;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

import jdk.internal.misc.JavaIOFileDescriptorAccess;
import jdk.internal.misc.SharedSecrets;
import jdk.internal.ref.PhantomCleanable;

public final class FileDescriptor {

    private int fd;

    private long handle;

    private Closeable parent;
    private List<Closeable> otherParents;
    private boolean closed;

    private boolean append;

    static {
        initIDs();
    }

    static {
        SharedSecrets.setJavaIOFileDescriptorAccess(
                new JavaIOFileDescriptorAccess() {
                    public void set(FileDescriptor fdo, int fd) {
                        fdo.set(fd);
                    }

                    public int get(FileDescriptor fdo) {
                        return fdo.fd;
                    }

                    public void setAppend(FileDescriptor fdo, boolean append) {
                        fdo.append = append;
                    }

                    public boolean getAppend(FileDescriptor fdo) {
                        return fdo.append;
                    }

                    public void close(FileDescriptor fdo) throws IOException {
                        fdo.close();
                    }

                    public void registerCleanup(FileDescriptor fdo) {
                        FileCleanable.register(fdo);
                    }

                    public void registerCleanup(FileDescriptor fdo,
                                                PhantomCleanable<FileDescriptor> cleanup) {
                        fdo.registerCleanup(cleanup);
                    }

                    public void unregisterCleanup(FileDescriptor fdo) {
                        fdo.unregisterCleanup();
                    }

                    public void setHandle(FileDescriptor fdo, long handle) {
                        fdo.setHandle(handle);
                    }

                    public long getHandle(FileDescriptor fdo) {
                        return fdo.handle;
                    }
                }
        );
    }

    private PhantomCleanable<FileDescriptor> cleanup;

    // SharedSecrets 读取面接缝（jdk.internal.misc.SharedSecrets.getJavaIOFileDescriptorAccess
    // 惰性触发）：调用即保证本类 <clinit> 已跑（访问面注册完成）。语义空实现。
    public static void ensureAccessRegistered() {
    }

    public FileDescriptor() {
        fd = -1;
        handle = -1;
    }

    private FileDescriptor(int fd) {
        this.fd = fd;
        this.handle = getHandle(fd);
        this.append = getAppend(fd);
    }

    public static final FileDescriptor in = new FileDescriptor(0);

    public static final FileDescriptor out = new FileDescriptor(1);

    public static final FileDescriptor err = new FileDescriptor(2);

    public boolean valid() {
        return (handle != -1) || (fd != -1);
    }

    public native void sync() throws SyncFailedException;

    private static native void initIDs();

    private static native long getHandle(int d);

    private static native boolean getAppend(int fd);

    @SuppressWarnings("unchecked")
    synchronized void set(int fd) {
        if (fd == -1 && cleanup != null) {
            cleanup.clear();
            cleanup = null;
        }
        this.fd = fd;
    }

    @SuppressWarnings("unchecked")
    void setHandle(long handle) {
        if (handle == -1 && cleanup != null) {
            cleanup.clear();
            cleanup = null;
        }
        this.handle = handle;
    }

    @SuppressWarnings("unchecked")
    synchronized void registerCleanup(PhantomCleanable<FileDescriptor> cleanable) {
        Objects.requireNonNull(cleanable, "cleanable");
        if (cleanup != null) {
            cleanup.clear();
        }
        cleanup = cleanable;
    }

    synchronized void unregisterCleanup() {
        if (cleanup != null) {
            cleanup.clear();
        }
        cleanup = null;
    }

    @SuppressWarnings("unchecked")
    synchronized void close() throws IOException {
        unregisterCleanup();
        close0();
    }

    private native void close0() throws IOException;

    synchronized void attach(Closeable c) {
        if (parent == null) {

            parent = c;
        } else if (otherParents == null) {
            otherParents = new ArrayList<>();
            otherParents.add(parent);
            otherParents.add(c);
        } else {
            otherParents.add(c);
        }
    }

    @SuppressWarnings("try")
    synchronized void closeAll(Closeable releaser) throws IOException {
        if (!closed) {
            closed = true;
            IOException ioe = null;
            try (releaser) {
                if (otherParents != null) {
                    for (Closeable referent : otherParents) {
                        try {
                            referent.close();
                        } catch(IOException x) {
                            if (ioe == null) {
                                ioe = x;
                            } else {
                                ioe.addSuppressed(x);
                            }
                        }
                    }
                }
            } catch(IOException ex) {

                if (ioe != null)
                    ex.addSuppressed(ioe);
                ioe = ex;
            } finally {
                if (ioe != null)
                    throw ioe;
            }
        }
    }
}
