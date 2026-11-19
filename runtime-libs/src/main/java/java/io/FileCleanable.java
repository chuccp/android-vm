package java.io;

import jdk.internal.misc.JavaIOFileDescriptorAccess;
import jdk.internal.misc.SharedSecrets;
import jdk.internal.ref.CleanerFactory;
import jdk.internal.ref.PhantomCleanable;

import java.lang.ref.Cleaner;

final class FileCleanable extends PhantomCleanable<FileDescriptor> {

    private static final JavaIOFileDescriptorAccess fdAccess =
            SharedSecrets.getJavaIOFileDescriptorAccess();

    private static native void cleanupClose0(int fd, long handle) throws IOException;

    private final int fd;

    private final long handle;

    static void register(FileDescriptor fdo) {
        if (fdo != null && fdo.valid()) {
            int fd = fdAccess.get(fdo);
            long handle = fdAccess.getHandle(fdo);
            fdo.registerCleanup(new FileCleanable(fdo, CleanerFactory.cleaner(), fd, handle));
        }
    }

    static void unregister(FileDescriptor fdo) {
        if (fdo != null) {
            fdo.unregisterCleanup();
        }
    }

    private FileCleanable(FileDescriptor obj, Cleaner cleaner, int fd, long handle) {
        super(obj, cleaner);
        this.fd = fd;
        this.handle = handle;
    }

    @Override
    protected void performCleanup() {
        try {
            cleanupClose0(fd, handle);
        } catch (IOException ioe) {
            throw new UncheckedIOException("close", ioe);
        }
    }
}
