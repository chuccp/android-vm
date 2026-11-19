package java.io;

import java.net.URI;
import java.nio.file.Path;

public final class File implements Serializable, Comparable<File> {
    public static final String separator = "/";
    public static final char separatorChar = '/';
    public static final String pathSeparator = ":";
    public static final char pathSeparatorChar = ':';

    private String path;

    public File(String path) {
        if (path == null) throw new NullPointerException("path");
        this.path = resolvePath(null, path);
    }

    public File(String parent, String child) {
        if (child == null) throw new NullPointerException("child");
        this.path = resolvePath(parent, child);
    }

    public File(File parent, String child) {
        this(parent == null ? null : parent.getPath(), child);
    }

    public String getPath() {
        return path;
    }

    public String getName() {
        int slash = path.lastIndexOf('/');
        return slash < 0 ? path : path.substring(slash + 1);
    }

    public String getParent() {
        int slash = path.lastIndexOf('/');
        if (slash <= 0) return null;
        return path.substring(0, slash);
    }

    public File getParentFile() {
        String parent = getParent();
        return parent == null ? null : new File(parent);
    }

    public String getAbsolutePath() {
        return path;
    }

    public File getAbsoluteFile() {
        return new File(path);
    }

    public String getCanonicalPath() throws IOException {
        return path;
    }

    public File getCanonicalFile() throws IOException {
        return new File(getCanonicalPath());
    }

    public Path toPath() {
        return Path.of(path);
    }

    public URI toURI() {
        return nativeToURI(getAbsolutePath());
    }

    public boolean isAbsolute() {
        return path.startsWith("/");
    }

    public boolean exists() {
        return nativeExists(path);
    }

    public boolean isFile() {
        return nativeIsFile(path);
    }

    public boolean isDirectory() {
        return nativeIsDirectory(path);
    }

    public long length() {
        return nativeLength(path);
    }

    public boolean canRead() {
        return nativeCanRead(path);
    }

    public boolean canWrite() {
        return nativeCanWrite(path);
    }

    public boolean canExecute() {
        return nativeCanExecute(path);
    }

    public boolean setReadable(boolean readable, boolean ownerOnly) {
        return nativeSetReadable(path, readable, ownerOnly);
    }

    public boolean setReadable(boolean readable) {
        return setReadable(readable, true);
    }

    public boolean setWritable(boolean writable, boolean ownerOnly) {
        return nativeSetWritable(path, writable, ownerOnly);
    }

    public boolean setWritable(boolean writable) {
        return setWritable(writable, true);
    }

    public boolean setExecutable(boolean executable, boolean ownerOnly) {
        return nativeSetExecutable(path, executable, ownerOnly);
    }

    public boolean setExecutable(boolean executable) {
        return setExecutable(executable, true);
    }

    public boolean setReadOnly() {
        return setWritable(false, false) &&
                setReadable(true, false) &&
                setExecutable(false, false);
    }

    public long lastModified() {
        return nativeLastModified(path);
    }

    public boolean setLastModified(long time) {
        return time >= 0 && nativeSetLastModified(path, time);
    }

    public long getFreeSpace() {
        return nativeSpace(path, 0);
    }

    public long getUsableSpace() {
        return nativeSpace(path, 1);
    }

    public long getTotalSpace() {
        return nativeSpace(path, 2);
    }

    public static File createTempFile(String prefix, String suffix)
            throws IOException {
        return createTempFile(prefix, suffix, null);
    }

    public static File createTempFile(
            String prefix, String suffix, File directory)
            throws IOException {
        if (prefix == null) throw new NullPointerException("prefix");
        if (prefix.length() < 3) {
            throw new IllegalArgumentException("Prefix string too short");
        }
        String normalizedSuffix = suffix == null ? ".tmp" : suffix;
        // 默认目录取 java.io.tmpdir（JDK 同口径）；属性目录不可写时回退相对
        // CWD（宿主环境兼容——旧行为）
        String propDir = directory == null ? System.getProperty("java.io.tmpdir") : null;
        String parent = directory == null ? propDir : directory.getPath();
        for (int attempt = 0; attempt < 1024; attempt++) {
            File candidate = new File(
                    parent,
                    prefix + Long.toHexString(
                            System.nanoTime() + attempt) +
                            normalizedSuffix);
            if (candidate.createNewFile()) return candidate;
        }
        if (directory == null && parent != null) {
            for (int attempt = 0; attempt < 1024; attempt++) {
                File candidate = new File(
                        (String) null,
                        prefix + Long.toHexString(
                                System.nanoTime() + attempt) +
                                normalizedSuffix);
                if (candidate.createNewFile()) return candidate;
            }
        }
        throw new IOException("Unable to create temporary file");
    }

    public void deleteOnExit() {
        nativeDeleteOnExit(path);
    }

    public boolean mkdir() {
        return nativeMkdir(path, false);
    }

    public boolean mkdirs() {
        return nativeMkdir(path, true);
    }

    public boolean createNewFile() throws IOException {
        return nativeCreate(path);
    }

    public boolean delete() {
        return nativeDelete(path);
    }

    public boolean renameTo(File destination) {
        if (destination == null) throw new NullPointerException("destination");
        return nativeRename(path, destination.getPath());
    }

    public String[] list() {
        String[] paths = nativeList(path);
        if (paths == null) return null;
        String[] names = new String[paths.length];
        for (int index = 0; index < paths.length; index++) {
            int slash = paths[index].lastIndexOf('/');
            names[index] = slash < 0
                    ? paths[index]
                    : paths[index].substring(slash + 1);
        }
        return names;
    }

    public File[] listFiles() {
        String[] paths = nativeList(path);
        if (paths == null) return null;
        File[] files = new File[paths.length];
        for (int index = 0; index < paths.length; index++) {
            files[index] = new File(paths[index]);
        }
        return files;
    }

    public String[] list(FilenameFilter filter) {
        String[] names = list();
        if (names == null || filter == null) return names;
        int count = 0;
        for (String name : names) {
            if (filter.accept(this, name)) names[count++] = name;
        }
        String[] filtered = new String[count];
        for (int index = 0; index < count; index++) {
            filtered[index] = names[index];
        }
        return filtered;
    }

    public File[] listFiles(FileFilter filter) {
        File[] files = listFiles();
        if (files == null || filter == null) return files;
        int count = 0;
        for (File file : files) {
            if (filter.accept(file)) files[count++] = file;
        }
        File[] filtered = new File[count];
        for (int index = 0; index < count; index++) {
            filtered[index] = files[index];
        }
        return filtered;
    }

    public File[] listFiles(FilenameFilter filter) {
        File[] files = listFiles();
        if (files == null || filter == null) return files;
        int count = 0;
        for (File file : files) {
            if (filter.accept(this, file.getName())) {
                files[count++] = file;
            }
        }
        File[] filtered = new File[count];
        for (int index = 0; index < count; index++) {
            filtered[index] = files[index];
        }
        return filtered;
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof File &&
                path.equals(((File) other).path);
    }

    @Override
    public int hashCode() {
        return path.hashCode() ^ 1234321;
    }

    @Override
    public int compareTo(File other) {
        return path.compareTo(other.path);
    }

    @Override
    public String toString() {
        return path;
    }

    private static native String resolvePath(String parent, String child);
    private static native URI nativeToURI(String path);
    private static native boolean nativeExists(String path);
    private static native boolean nativeIsFile(String path);
    private static native boolean nativeIsDirectory(String path);
    private static native long nativeLength(String path);
    private static native boolean nativeCanRead(String path);
    private static native boolean nativeCanWrite(String path);
    private static native boolean nativeCanExecute(String path);
    private static native boolean nativeSetReadable(
            String path, boolean readable, boolean ownerOnly);
    private static native boolean nativeSetWritable(
            String path, boolean writable, boolean ownerOnly);
    private static native boolean nativeSetExecutable(
            String path, boolean executable, boolean ownerOnly);
    private static native long nativeLastModified(String path);
    private static native boolean nativeSetLastModified(String path, long time);
    private static native long nativeSpace(String path, int kind);
    private static native void nativeDeleteOnExit(String path);
    private static native boolean nativeMkdir(String path, boolean recursive);
    private static native boolean nativeCreate(String path);
    private static native boolean nativeDelete(String path);
    private static native boolean nativeRename(String source, String target);
    private static native String[] nativeList(String path);

    // ---- overlay：isInvalid / getPrefixLength（OpenJDK 内部面）----
    boolean isInvalid() {
        return false;
    }

    int getPrefixLength() {
        return getPrefixLength(path);
    }

    static int getPrefixLength(String path) {
        if (path.isEmpty()) return 0;
        if (path.charAt(0) == '/') return 1;
        return 0;
    }

}
