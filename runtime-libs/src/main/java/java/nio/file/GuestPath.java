package java.nio.file;

import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.NoSuchElementException;

final class GuestPath implements Path {
    private final String path;

    GuestPath(String path) {
        if (path == null) throw new NullPointerException("path");
        this.path = path;
    }

    private static List<String> names(String value) {
        List<String> result = new ArrayList<String>();
        int start = value.startsWith("/") ? 1 : 0;
        while (start < value.length()) {
            int end = value.indexOf('/', start);
            if (end < 0) end = value.length();
            if (end > start) result.add(value.substring(start, end));
            start = end + 1;
        }
        if (result.isEmpty() && !value.startsWith("/")) {
            result.add("");
        }
        return result;
    }

    private static String join(
            boolean absolute, List<String> names, int from, int to) {
        StringBuilder result = new StringBuilder();
        if (absolute) result.append('/');
        for (int index = from; index < to; index++) {
            if (index > from || (absolute && result.length() > 1)) {
                result.append('/');
            }
            result.append(names.get(index));
        }
        return result.toString();
    }

    @Override
    public FileSystem getFileSystem() {
        return GuestFileSystem.INSTANCE;
    }

    @Override
    public boolean isAbsolute() {
        return path.startsWith("/");
    }

    @Override
    public Path getRoot() {
        return isAbsolute() ? new GuestPath("/") : null;
    }

    @Override
    public Path getFileName() {
        List<String> names = names(path);
        return names.isEmpty()
                ? null
                : new GuestPath(names.get(names.size() - 1));
    }

    @Override
    public Path getParent() {
        List<String> names = names(path);
        if (names.isEmpty() || names.size() == 1 && !isAbsolute()) {
            return null;
        }
        return new GuestPath(join(isAbsolute(), names, 0, names.size() - 1));
    }

    @Override
    public int getNameCount() {
        return names(path).size();
    }

    @Override
    public Path getName(int index) {
        List<String> names = names(path);
        if (index < 0 || index >= names.size()) {
            throw new IllegalArgumentException("name index");
        }
        return new GuestPath(names.get(index));
    }

    @Override
    public Path subpath(int beginIndex, int endIndex) {
        List<String> names = names(path);
        if (beginIndex < 0 || beginIndex >= endIndex ||
                endIndex > names.size()) {
            throw new IllegalArgumentException("subpath range");
        }
        return new GuestPath(join(false, names, beginIndex, endIndex));
    }

    private static boolean startsWithNames(
            List<String> value, List<String> prefix) {
        if (prefix.size() > value.size()) return false;
        for (int index = 0; index < prefix.size(); index++) {
            if (!value.get(index).equals(prefix.get(index))) return false;
        }
        return true;
    }

    @Override
    public boolean startsWith(Path other) {
        return startsWithNames(names(path), names(other.toString()));
    }

    @Override
    public boolean endsWith(Path other) {
        List<String> value = names(path);
        List<String> suffix = names(other.toString());
        if (suffix.size() > value.size()) return false;
        int offset = value.size() - suffix.size();
        for (int index = 0; index < suffix.size(); index++) {
            if (!value.get(offset + index).equals(suffix.get(index))) {
                return false;
            }
        }
        return true;
    }

    @Override
    public Path normalize() {
        boolean absolute = isAbsolute();
        List<String> normalized = new ArrayList<String>();
        for (String name : names(path)) {
            if (name.isEmpty() || ".".equals(name)) continue;
            if ("..".equals(name)) {
                if (!normalized.isEmpty() &&
                        !"..".equals(normalized.get(normalized.size() - 1))) {
                    normalized.remove(normalized.size() - 1);
                } else if (!absolute) {
                    normalized.add(name);
                }
            } else {
                normalized.add(name);
            }
        }
        return new GuestPath(join(absolute, normalized, 0, normalized.size()));
    }

    @Override
    public Path resolve(Path other) {
        if (other.isAbsolute() || path.isEmpty()) {
            return other;
        }
        if (other.toString().isEmpty()) return this;
        return new GuestPath(path +
                (path.endsWith("/") ? "" : "/") +
                other.toString());
    }

    @Override
    public Path relativize(Path other) {
        if (isAbsolute() != other.isAbsolute()) {
            throw new IllegalArgumentException("mixed absolute and relative paths");
        }
        List<String> left = names(path);
        List<String> right = names(other.toString());
        int common = 0;
        while (common < left.size() && common < right.size() &&
                left.get(common).equals(right.get(common))) {
            common++;
        }
        List<String> result = new ArrayList<String>();
        for (int index = common; index < left.size(); index++) {
            result.add("..");
        }
        for (int index = common; index < right.size(); index++) {
            result.add(right.get(index));
        }
        return new GuestPath(join(false, result, 0, result.size()));
    }

    @Override
    public URI toUri() {
        return nativeToUri(toAbsolutePath().toString());
    }

    @Override
    public Path toAbsolutePath() {
        return isAbsolute() ? this : new GuestPath(new File(path).getAbsolutePath());
    }

    @Override
    public Path toRealPath(LinkOption... options) throws IOException {
        File file = toFile();
        if (!file.exists()) {
            throw new NoSuchFileException(path);
        }
        return new GuestPath(file.getCanonicalPath());
    }

    @Override
    public File toFile() {
        return new File(path);
    }

    @Override
    public WatchKey register(
            WatchService watcher,
            WatchEvent.Kind<?>[] events,
            WatchEvent.Modifier... modifiers) throws IOException {
        throw new UnsupportedOperationException(
                "Guest watch service is not available");
    }

    @Override
    public Iterator<Path> iterator() {
        final List<String> names = names(path);
        return new Iterator<Path>() {
            private int index;

            @Override
            public boolean hasNext() {
                return index < names.size();
            }

            @Override
            public Path next() {
                if (!hasNext()) throw new NoSuchElementException();
                return new GuestPath(names.get(index++));
            }
        };
    }

    @Override
    public int compareTo(Path other) {
        return path.compareTo(other.toString());
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof GuestPath &&
                path.equals(((GuestPath) other).path);
    }

    @Override
    public int hashCode() {
        return path.hashCode() ^ 0x5f5f5f5f;
    }

    @Override
    public String toString() {
        return path;
    }

    private static native URI nativeToUri(String path);
}
