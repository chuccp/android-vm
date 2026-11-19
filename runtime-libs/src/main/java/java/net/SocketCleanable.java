package java.net;

import jdk.internal.misc.JavaIOFileDescriptorAccess;
import jdk.internal.misc.SharedSecrets;
import jdk.internal.ref.CleanerFactory;
import jdk.internal.ref.PhantomCleanable;

import java.io.FileDescriptor;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.lang.ref.Cleaner;

final class SocketCleanable extends PhantomCleanable<FileDescriptor> {

    private static final JavaIOFileDescriptorAccess fdAccess =
            SharedSecrets.getJavaIOFileDescriptorAccess();

    private static native void cleanupClose0(int fd) throws IOException;

    private final int fd;

    static void register(FileDescriptor fdo) {
        if (fdo != null && fdo.valid()) {
            int fd = fdAccess.get(fdo);
            fdAccess.registerCleanup(fdo,
                    new SocketCleanable(fdo, CleanerFactory.cleaner(), fd));
        }
    }

    static void unregister(FileDescriptor fdo) {
        if (fdo != null) {
            fdAccess.unregisterCleanup(fdo);
        }
    }

    private SocketCleanable(FileDescriptor obj, Cleaner cleaner, int fd) {
        super(obj, cleaner);
        this.fd = fd;
    }

    @Override
    protected void performCleanup() {
        try {
            cleanupClose0(fd);
        } catch (IOException ioe) {
            throw new UncheckedIOException("close", ioe);
        }
    }
}
