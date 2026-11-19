package jdk.internal.misc;

public interface JavaIOFileDescriptorAccess {
    void set(java.io.FileDescriptor fdo, int fd);
    int get(java.io.FileDescriptor fdo);
    void setAppend(java.io.FileDescriptor fdo, boolean append);
    boolean getAppend(java.io.FileDescriptor fdo);
    void close(java.io.FileDescriptor fdo) throws java.io.IOException;
    void registerCleanup(java.io.FileDescriptor fdo);
    void registerCleanup(java.io.FileDescriptor fdo,
                        jdk.internal.ref.PhantomCleanable<java.io.FileDescriptor> cleanup);
    void unregisterCleanup(java.io.FileDescriptor fdo);
    void setHandle(java.io.FileDescriptor fdo, long handle);
    long getHandle(java.io.FileDescriptor fdo);
}
