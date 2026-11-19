package java.io;

import java.nio.channels.FileChannel;
import jdk.internal.misc.SharedSecrets;
import jdk.internal.misc.JavaIOFileDescriptorAccess;
import sun.nio.ch.FileChannelImpl;

public
class FileOutputStream extends OutputStream
{

    private static final JavaIOFileDescriptorAccess fdAccess =
        SharedSecrets.getJavaIOFileDescriptorAccess();

    private final FileDescriptor fd;

    private volatile FileChannel channel;

    private final String path;

    private final Object closeLock = new Object();

    private volatile boolean closed;

    private final Object altFinalizer;

    public FileOutputStream(String name) throws FileNotFoundException {
        this(name != null ? new File(name) : null, false);
    }

    public FileOutputStream(String name, boolean append)
        throws FileNotFoundException
    {
        this(name != null ? new File(name) : null, append);
    }

    public FileOutputStream(File file) throws FileNotFoundException {
        this(file, false);
    }

    public FileOutputStream(File file, boolean append)
        throws FileNotFoundException
    {
        String name = (file != null ? file.getPath() : null);
        SecurityManager security = System.getSecurityManager();
        if (security != null) {
            security.checkWrite(name);
        }
        if (name == null) {
            throw new NullPointerException();
        }
        if (file.isInvalid()) {
            throw new FileNotFoundException("Invalid file path");
        }
        this.fd = new FileDescriptor();
        fd.attach(this);
        this.path = name;

        open(name, append);
        altFinalizer = getFinalizer(this);
        if (altFinalizer == null) {
            FileCleanable.register(fd);
        }
    }

    public FileOutputStream(FileDescriptor fdObj) {
        SecurityManager security = System.getSecurityManager();
        if (fdObj == null) {
            throw new NullPointerException();
        }
        if (security != null) {
            security.checkWrite(fdObj);
        }
        this.fd = fdObj;
        this.path = null;
        this.altFinalizer = null;

        fd.attach(this);
    }

    private native void open0(String name, boolean append)
        throws FileNotFoundException;

    private void open(String name, boolean append)
        throws FileNotFoundException {
        open0(name, append);
    }

    private native void write(int b, boolean append) throws IOException;

    public void write(int b) throws IOException {
        write(b, fdAccess.getAppend(fd));
    }

    private native void writeBytes(byte b[], int off, int len, boolean append)
        throws IOException;

    public void write(byte b[]) throws IOException {
        writeBytes(b, 0, b.length, fdAccess.getAppend(fd));
    }

    public void write(byte b[], int off, int len) throws IOException {
        writeBytes(b, off, len, fdAccess.getAppend(fd));
    }

    public void close() throws IOException {
        if (closed) {
            return;
        }
        synchronized (closeLock) {
            if (closed) {
                return;
            }
            closed = true;
        }

        FileChannel fc = channel;
        if (fc != null) {

            fc.close();
        }

        fd.closeAll(new Closeable() {
            public void close() throws IOException {
               fd.close();
           }
        });
    }

     public final FileDescriptor getFD()  throws IOException {
        if (fd != null) {
            return fd;
        }
        throw new IOException();
     }

    public FileChannel getChannel() {
        FileChannel fc = this.channel;
        if (fc == null) {
            synchronized (this) {
                fc = this.channel;
                if (fc == null) {
                    this.channel = fc = FileChannelImpl.open(fd, path, false,
                        true, false, this);
                    if (closed) {
                        try {

                            fc.close();
                        } catch (IOException ioe) {
                            throw new InternalError(ioe);
                        }
                    }
                }
            }
        }
        return fc;
    }

    @Deprecated(since="9", forRemoval = true)
    protected void finalize() throws IOException {
    }

    private static native void initIDs();

    static {
        initIDs();
    }

    private static Object getFinalizer(FileOutputStream fos) {
        Class<?> clazz = fos.getClass();
        while (clazz != FileOutputStream.class) {
            try {
                clazz.getDeclaredMethod("close");
                return new AltFinalizer(fos);
            } catch (NoSuchMethodException nsme) {

            }
            clazz = clazz.getSuperclass();
        }
        return null;
    }

    static class AltFinalizer {
        private final FileOutputStream fos;

        AltFinalizer(FileOutputStream fos) {
            this.fos = fos;
        }

        @Override
        @SuppressWarnings("deprecation")
        protected final void finalize() {
            try {
                if (fos.fd != null) {
                    if (fos.fd == FileDescriptor.out || fos.fd == FileDescriptor.err) {

                        fos.flush();
                    } else {

                        fos.close();
                    }
                }
            } catch (IOException ioe) {

            }
        }
    }

}
