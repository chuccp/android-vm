package java.lang;

import java.lang.ProcessBuilder.Redirect;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayInputStream;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;
import java.util.EnumSet;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.security.AccessController;
import static java.security.AccessController.doPrivileged;
import java.security.PrivilegedAction;
import java.security.PrivilegedActionException;
import java.security.PrivilegedExceptionAction;
import java.util.Properties;
import jdk.internal.misc.JavaIOFileDescriptorAccess;
import jdk.internal.misc.SharedSecrets;
import jdk.internal.util.StaticProperty;
import sun.security.action.GetPropertyAction;

final class ProcessImpl extends Process {
    private static final JavaIOFileDescriptorAccess fdAccess
        = SharedSecrets.getJavaIOFileDescriptorAccess();

    static final boolean SUPPORTS_NORMAL_TERMINATION = true;

    private final int pid;
    private final ProcessHandleImpl processHandle;
    private int exitcode;
    private boolean hasExited;

    private  OutputStream stdin;
    private  InputStream  stdout;
    private  InputStream  stderr;

    private  DeferredCloseInputStream stdout_inner_stream;

    private static enum LaunchMechanism {

        FORK,
        POSIX_SPAWN,
        VFORK
    }

    private static enum Platform {

        LINUX(LaunchMechanism.VFORK, LaunchMechanism.POSIX_SPAWN, LaunchMechanism.FORK),

        BSD(LaunchMechanism.POSIX_SPAWN, LaunchMechanism.FORK),

        SOLARIS(LaunchMechanism.POSIX_SPAWN, LaunchMechanism.FORK),

        AIX(LaunchMechanism.POSIX_SPAWN, LaunchMechanism.FORK);

        final LaunchMechanism defaultLaunchMechanism;
        final Set<LaunchMechanism> validLaunchMechanisms;

        Platform(LaunchMechanism ... launchMechanisms) {
            this.defaultLaunchMechanism = launchMechanisms[0];
            this.validLaunchMechanisms =
                EnumSet.copyOf(Arrays.asList(launchMechanisms));
        }

        @SuppressWarnings("fallthrough")
        private String helperPath(String javahome, String osArch) {
            switch (this) {
                case SOLARIS:

                case LINUX:
                case AIX:
                case BSD:
                    return javahome + "/lib/jspawnhelper";

                default:
                    throw new AssertionError("Unsupported platform: " + this);
            }
        }

        String helperPath() {
            Properties props = GetPropertyAction.privilegedGetProperties();
            return helperPath(StaticProperty.javaHome(),
                              props.getProperty("os.arch"));
        }

        LaunchMechanism launchMechanism() {
            return AccessController.doPrivileged(
                (PrivilegedAction<LaunchMechanism>) () -> {
                    String s = System.getProperty(
                        "jdk.lang.Process.launchMechanism");
                    LaunchMechanism lm;
                    if (s == null) {
                        lm = defaultLaunchMechanism;
                        s = lm.name().toLowerCase(Locale.ENGLISH);
                    } else {
                        try {
                            lm = LaunchMechanism.valueOf(
                                s.toUpperCase(Locale.ENGLISH));
                        } catch (IllegalArgumentException e) {
                            lm = null;
                        }
                    }
                    if (lm == null || !validLaunchMechanisms.contains(lm)) {
                        throw new Error(
                            s + " is not a supported " +
                            "process launch mechanism on this platform."
                        );
                    }
                    return lm;
                }
            );
        }

        static Platform get() {
            String osName = GetPropertyAction.privilegedGetProperty("os.name");

            if (osName.equals("Linux")) { return LINUX; }
            if (osName.contains("OS X")) { return BSD; }
            if (osName.equals("SunOS")) { return SOLARIS; }
            if (osName.equals("AIX")) { return AIX; }

            throw new Error(osName + " is not a supported OS platform.");
        }
    }

    private static final Platform platform = Platform.get();
    private static final LaunchMechanism launchMechanism = platform.launchMechanism();
    private static final byte[] helperpath = toCString(platform.helperPath());

    private static byte[] toCString(String s) {
        if (s == null)
            return null;
        byte[] bytes = s.getBytes();
        byte[] result = new byte[bytes.length + 1];
        System.arraycopy(bytes, 0,
                         result, 0,
                         bytes.length);
        result[result.length-1] = (byte)0;
        return result;
    }

    static Process start(String[] cmdarray,
                         java.util.Map<String,String> environment,
                         String dir,
                         ProcessBuilder.Redirect[] redirects,
                         boolean redirectErrorStream)
            throws IOException
    {
        assert cmdarray != null && cmdarray.length > 0;

        byte[][] args = new byte[cmdarray.length-1][];
        int size = args.length;
        for (int i = 0; i < args.length; i++) {
            args[i] = cmdarray[i+1].getBytes();
            size += args[i].length;
        }
        byte[] argBlock = new byte[size];
        int i = 0;
        for (byte[] arg : args) {
            System.arraycopy(arg, 0, argBlock, i, arg.length);
            i += arg.length + 1;

        }

        int[] envc = new int[1];
        byte[] envBlock = ProcessEnvironment.toEnvironmentBlock(environment, envc);

        int[] std_fds;

        FileInputStream  f0 = null;
        FileOutputStream f1 = null;
        FileOutputStream f2 = null;

        try {
            boolean forceNullOutputStream = false;
            if (redirects == null) {
                std_fds = new int[] { -1, -1, -1 };
            } else {
                std_fds = new int[3];

                if (redirects[0] == Redirect.PIPE) {
                    std_fds[0] = -1;
                } else if (redirects[0] == Redirect.INHERIT) {
                    std_fds[0] = 0;
                } else if (redirects[0] instanceof ProcessBuilder.RedirectPipeImpl) {
                    std_fds[0] = fdAccess.get(((ProcessBuilder.RedirectPipeImpl) redirects[0]).getFd());
                } else {
                    f0 = new FileInputStream(redirects[0].file());
                    std_fds[0] = fdAccess.get(f0.getFD());
                }

                if (redirects[1] == Redirect.PIPE) {
                    std_fds[1] = -1;
                } else if (redirects[1] == Redirect.INHERIT) {
                    std_fds[1] = 1;
                } else if (redirects[1] instanceof ProcessBuilder.RedirectPipeImpl) {
                    std_fds[1] = fdAccess.get(((ProcessBuilder.RedirectPipeImpl) redirects[1]).getFd());

                    forceNullOutputStream = true;
                } else {
                    f1 = new FileOutputStream(redirects[1].file(),
                            redirects[1].append());
                    std_fds[1] = fdAccess.get(f1.getFD());
                }

                if (redirects[2] == Redirect.PIPE) {
                    std_fds[2] = -1;
                } else if (redirects[2] == Redirect.INHERIT) {
                    std_fds[2] = 2;
                } else if (redirects[2] instanceof ProcessBuilder.RedirectPipeImpl) {
                    std_fds[2] = fdAccess.get(((ProcessBuilder.RedirectPipeImpl) redirects[2]).getFd());
                } else {
                    f2 = new FileOutputStream(redirects[2].file(),
                            redirects[2].append());
                    std_fds[2] = fdAccess.get(f2.getFD());
                }
            }

            Process p = new ProcessImpl
                    (toCString(cmdarray[0]),
                            argBlock, args.length,
                            envBlock, envc[0],
                            toCString(dir),
                            std_fds,
                            forceNullOutputStream,
                            redirectErrorStream);
            if (redirects != null) {

                if (std_fds[0] >= 0 &&
                        redirects[0] instanceof ProcessBuilder.RedirectPipeImpl) {
                    fdAccess.set(((ProcessBuilder.RedirectPipeImpl) redirects[0]).getFd(), std_fds[0]);
                }
                if (std_fds[1] >= 0 &&
                        redirects[1] instanceof ProcessBuilder.RedirectPipeImpl) {
                    fdAccess.set(((ProcessBuilder.RedirectPipeImpl) redirects[1]).getFd(), std_fds[1]);
                }
                if (std_fds[2] >= 0 &&
                        redirects[2] instanceof ProcessBuilder.RedirectPipeImpl) {
                    fdAccess.set(((ProcessBuilder.RedirectPipeImpl) redirects[2]).getFd(), std_fds[2]);
                }
            }
            return p;
        } finally {

            try { if (f0 != null) f0.close(); }
            finally {
                try { if (f1 != null) f1.close(); }
                finally { if (f2 != null) f2.close(); }
            }
        }
    }

    private native int forkAndExec(int mode, byte[] helperpath,
                                   byte[] prog,
                                   byte[] argBlock, int argc,
                                   byte[] envBlock, int envc,
                                   byte[] dir,
                                   int[] fds,
                                   boolean redirectErrorStream)
        throws IOException;

    private ProcessImpl(final byte[] prog,
                final byte[] argBlock, final int argc,
                final byte[] envBlock, final int envc,
                final byte[] dir,
                final int[] fds,
                final boolean forceNullOutputStream,
                final boolean redirectErrorStream)
            throws IOException {

        pid = forkAndExec(launchMechanism.ordinal() + 1,
                          helperpath,
                          prog,
                          argBlock, argc,
                          envBlock, envc,
                          dir,
                          fds,
                          redirectErrorStream);
        processHandle = ProcessHandleImpl.getInternal(pid);

        try {
            doPrivileged((PrivilegedExceptionAction<Void>) () -> {
                initStreams(fds, forceNullOutputStream);
                return null;
            });
        } catch (PrivilegedActionException ex) {
            throw (IOException) ex.getException();
        }
    }

    static FileDescriptor newFileDescriptor(int fd) {
        FileDescriptor fileDescriptor = new FileDescriptor();
        fdAccess.set(fileDescriptor, fd);
        return fileDescriptor;
    }

    void initStreams(int[] fds, boolean forceNullOutputStream) throws IOException {
        switch (platform) {
            case LINUX:
            case BSD:
                stdin = (fds[0] == -1) ?
                        ProcessBuilder.NullOutputStream.INSTANCE :
                        new ProcessPipeOutputStream(fds[0]);

                stdout = (fds[1] == -1 || forceNullOutputStream) ?
                         ProcessBuilder.NullInputStream.INSTANCE :
                         new ProcessPipeInputStream(fds[1]);

                stderr = (fds[2] == -1) ?
                         ProcessBuilder.NullInputStream.INSTANCE :
                         new ProcessPipeInputStream(fds[2]);

                ProcessHandleImpl.completion(pid, true).handle((exitcode, throwable) -> {
                    synchronized (this) {
                        this.exitcode = (exitcode == null) ? -1 : exitcode.intValue();
                        this.hasExited = true;
                        this.notifyAll();
                    }

                    if (stdout instanceof ProcessPipeInputStream)
                        ((ProcessPipeInputStream) stdout).processExited();

                    if (stderr instanceof ProcessPipeInputStream)
                        ((ProcessPipeInputStream) stderr).processExited();

                    if (stdin instanceof ProcessPipeOutputStream)
                        ((ProcessPipeOutputStream) stdin).processExited();

                    return null;
                });
                break;

            case SOLARIS:
                stdin = (fds[0] == -1) ?
                        ProcessBuilder.NullOutputStream.INSTANCE :
                        new BufferedOutputStream(
                            new FileOutputStream(newFileDescriptor(fds[0])));

                stdout = (fds[1] == -1 || forceNullOutputStream) ?
                         ProcessBuilder.NullInputStream.INSTANCE :
                         new BufferedInputStream(
                             stdout_inner_stream =
                                 new DeferredCloseInputStream(
                                     newFileDescriptor(fds[1])));

                stderr = (fds[2] == -1) ?
                         ProcessBuilder.NullInputStream.INSTANCE :
                         new DeferredCloseInputStream(newFileDescriptor(fds[2]));

                ProcessHandleImpl.completion(pid, true).handle((exitcode, throwable) -> {
                    synchronized (this) {
                        this.exitcode = (exitcode == null) ? -1 : exitcode.intValue();
                        this.hasExited = true;
                        this.notifyAll();
                    }
                    return null;
                });
                break;

            case AIX:
                stdin = (fds[0] == -1) ?
                        ProcessBuilder.NullOutputStream.INSTANCE :
                        new ProcessPipeOutputStream(fds[0]);

                stdout = (fds[1] == -1 || forceNullOutputStream) ?
                         ProcessBuilder.NullInputStream.INSTANCE :
                         new DeferredCloseProcessPipeInputStream(fds[1]);

                stderr = (fds[2] == -1) ?
                         ProcessBuilder.NullInputStream.INSTANCE :
                         new DeferredCloseProcessPipeInputStream(fds[2]);

                ProcessHandleImpl.completion(pid, true).handle((exitcode, throwable) -> {
                    synchronized (this) {
                        this.exitcode = (exitcode == null) ? -1 : exitcode.intValue();
                        this.hasExited = true;
                        this.notifyAll();
                    }

                    if (stdout instanceof DeferredCloseProcessPipeInputStream)
                        ((DeferredCloseProcessPipeInputStream) stdout).processExited();

                    if (stderr instanceof DeferredCloseProcessPipeInputStream)
                        ((DeferredCloseProcessPipeInputStream) stderr).processExited();

                    if (stdin instanceof ProcessPipeOutputStream)
                        ((ProcessPipeOutputStream) stdin).processExited();

                    return null;
                });
                break;

            default: throw new AssertionError("Unsupported platform: " + platform);
        }
    }

    public OutputStream getOutputStream() {
        return stdin;
    }

    public InputStream getInputStream() {
        return stdout;
    }

    public InputStream getErrorStream() {
        return stderr;
    }

    public synchronized int waitFor() throws InterruptedException {
        while (!hasExited) {
            wait();
        }
        return exitcode;
    }

    @Override
    public synchronized boolean waitFor(long timeout, TimeUnit unit)
        throws InterruptedException
    {
        long remainingNanos = unit.toNanos(timeout);
        if (hasExited) return true;
        if (timeout <= 0) return false;

        long deadline = System.nanoTime() + remainingNanos;
        do {
            TimeUnit.NANOSECONDS.timedWait(this, remainingNanos);
            if (hasExited) {
                return true;
            }
            remainingNanos = deadline - System.nanoTime();
        } while (remainingNanos > 0);
        return hasExited;
    }

    public synchronized int exitValue() {
        if (!hasExited) {
            throw new IllegalThreadStateException("process hasn't exited");
        }
        return exitcode;
    }

    private void destroy(boolean force) {
        switch (platform) {
            case LINUX:
            case BSD:
            case AIX:

                synchronized (this) {
                    if (!hasExited)
                        processHandle.destroyProcess(force);
                }
                try { stdin.close();  } catch (IOException ignored) {}
                try { stdout.close(); } catch (IOException ignored) {}
                try { stderr.close(); } catch (IOException ignored) {}
                break;

            case SOLARIS:

                synchronized (this) {
                    if (!hasExited)
                        processHandle.destroyProcess(force);
                    try {
                        stdin.close();
                        if (stdout_inner_stream != null)
                            stdout_inner_stream.closeDeferred(stdout);
                        if (stderr instanceof DeferredCloseInputStream)
                            ((DeferredCloseInputStream) stderr)
                                .closeDeferred(stderr);
                    } catch (IOException e) {

                    }
                }
                break;

            default: throw new AssertionError("Unsupported platform: " + platform);
        }
    }

    @Override
    public CompletableFuture<Process> onExit() {
        return ProcessHandleImpl.completion(pid, false)
                .handleAsync((unusedExitStatus, unusedThrowable) -> {
                    boolean interrupted = false;
                    while (true) {

                        try {
                            waitFor();
                            break;
                        } catch (InterruptedException ie) {
                            interrupted = true;
                        }
                    }
                    if (interrupted) {
                        Thread.currentThread().interrupt();
                    }
                    return this;
                });
    }

    @Override
    public ProcessHandle toHandle() {
        SecurityManager sm = System.getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new RuntimePermission("manageProcess"));
        }
        return processHandle;
    }

    @Override
    public boolean supportsNormalTermination() {
        return ProcessImpl.SUPPORTS_NORMAL_TERMINATION;
    }

    @Override
    public void destroy() {
        destroy(false);
    }

    @Override
    public Process destroyForcibly() {
        destroy(true);
        return this;
    }

    @Override
    public long pid() {
        return pid;
    }

    @Override
    public synchronized boolean isAlive() {
        return !hasExited;
    }

    @Override
    public String toString() {
        return new StringBuilder("Process[pid=").append(pid)
                .append(", exitValue=").append(hasExited ? exitcode : "\"not exited\"")
                .append("]").toString();
    }

    private static native void init();

    static {
        init();
    }

    private static class ProcessPipeInputStream extends BufferedInputStream {
        private final Object closeLock = new Object();

        ProcessPipeInputStream(int fd) {
            super(new PipeInputStream(newFileDescriptor(fd)));
        }
        private static byte[] drainInputStream(InputStream in)
                throws IOException {
            int n = 0;
            int j;
            byte[] a = null;
            while ((j = in.available()) > 0) {
                a = (a == null) ? new byte[j] : Arrays.copyOf(a, n + j);
                n += in.read(a, n, j);
            }
            return (a == null || n == a.length) ? a : Arrays.copyOf(a, n);
        }

        synchronized void processExited() {
            synchronized (closeLock) {
                try {
                    InputStream in = this.in;

                    if (in != null) {
                        byte[] stragglers = drainInputStream(in);
                        in.close();
                        this.in = (stragglers == null) ?
                            ProcessBuilder.NullInputStream.INSTANCE :
                            new ByteArrayInputStream(stragglers);
                    }
                } catch (IOException ignored) {}
            }
        }

        @Override
        public void close() throws IOException {

            synchronized (closeLock) {
                super.close();
            }
        }
    }

    private static class ProcessPipeOutputStream extends BufferedOutputStream {
        ProcessPipeOutputStream(int fd) {
            super(new FileOutputStream(newFileDescriptor(fd)));
        }

        synchronized void processExited() {
            OutputStream out = this.out;
            if (out != null) {
                try {
                    out.close();
                } catch (IOException ignored) {

                }
                this.out = ProcessBuilder.NullOutputStream.INSTANCE;
            }
        }
    }

    private static class DeferredCloseInputStream extends PipeInputStream {
        DeferredCloseInputStream(FileDescriptor fd) {
            super(fd);
        }

        private Object lock = new Object();
        private boolean closePending = false;
        private int useCount = 0;
        private InputStream streamToClose;

        private void raise() {
            synchronized (lock) {
                useCount++;
            }
        }

        private void lower() throws IOException {
            synchronized (lock) {
                useCount--;
                if (useCount == 0 && closePending) {
                    streamToClose.close();
                }
            }
        }

        private void closeDeferred(InputStream stc) throws IOException {
            synchronized (lock) {
                if (useCount == 0) {
                    stc.close();
                } else {
                    closePending = true;
                    streamToClose = stc;
                }
            }
        }

        public void close() throws IOException {
            synchronized (lock) {
                useCount = 0;
                closePending = false;
            }
            super.close();
        }

        public int read() throws IOException {
            raise();
            try {
                return super.read();
            } finally {
                lower();
            }
        }

        public int read(byte[] b) throws IOException {
            raise();
            try {
                return super.read(b);
            } finally {
                lower();
            }
        }

        public int read(byte[] b, int off, int len) throws IOException {
            raise();
            try {
                return super.read(b, off, len);
            } finally {
                lower();
            }
        }

        public long skip(long n) throws IOException {
            raise();
            try {
                return super.skip(n);
            } finally {
                lower();
            }
        }

        public int available() throws IOException {
            raise();
            try {
                return super.available();
            } finally {
                lower();
            }
        }
    }

    private static class DeferredCloseProcessPipeInputStream
        extends BufferedInputStream {

        private final Object closeLock = new Object();
        private int useCount = 0;
        private boolean closePending = false;

        DeferredCloseProcessPipeInputStream(int fd) {
            super(new PipeInputStream(newFileDescriptor(fd)));
        }

        private InputStream drainInputStream(InputStream in)
                throws IOException {
            int n = 0;
            int j;
            byte[] a = null;
            synchronized (closeLock) {
                if (buf == null)
                    return null;
                j = in.available();
            }
            while (j > 0) {
                a = (a == null) ? new byte[j] : Arrays.copyOf(a, n + j);
                synchronized (closeLock) {
                    if (buf == null)
                        return null;
                    n += in.read(a, n, j);
                    j = in.available();
                }
            }
            return (a == null) ?
                    ProcessBuilder.NullInputStream.INSTANCE :
                    new ByteArrayInputStream(n == a.length ? a : Arrays.copyOf(a, n));
        }

        synchronized void processExited() {
            try {
                InputStream in = this.in;
                if (in != null) {
                    InputStream stragglers = drainInputStream(in);
                    in.close();
                    this.in = stragglers;
                }
            } catch (IOException ignored) { }
        }

        private void raise() {
            synchronized (closeLock) {
                useCount++;
            }
        }

        private void lower() throws IOException {
            synchronized (closeLock) {
                useCount--;
                if (useCount == 0 && closePending) {
                    closePending = false;
                    super.close();
                }
            }
        }

        @Override
        public int read() throws IOException {
            raise();
            try {
                return super.read();
            } finally {
                lower();
            }
        }

        @Override
        public int read(byte[] b) throws IOException {
            raise();
            try {
                return super.read(b);
            } finally {
                lower();
            }
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            raise();
            try {
                return super.read(b, off, len);
            } finally {
                lower();
            }
        }

        @Override
        public long skip(long n) throws IOException {
            raise();
            try {
                return super.skip(n);
            } finally {
                lower();
            }
        }

        @Override
        public int available() throws IOException {
            raise();
            try {
                return super.available();
            } finally {
                lower();
            }
        }

        @Override
        public void close() throws IOException {

            synchronized (closeLock) {
                if (useCount == 0) {
                    super.close();
                }
                else {
                    closePending = true;
                }
            }
        }
    }
}
