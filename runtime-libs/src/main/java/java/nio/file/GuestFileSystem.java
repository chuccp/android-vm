package java.nio.file;

import java.io.IOException;
import java.nio.file.attribute.UserPrincipalLookupService;
import java.nio.file.spi.FileSystemProvider;
import java.util.Collection;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.util.Set;

final class GuestFileSystem extends FileSystem {
    static final GuestFileSystem INSTANCE = new GuestFileSystem();

    private final Iterable<Path> roots = new Iterable<Path>() {
        private boolean emitted;

        @Override
        public Iterator<Path> iterator() {
            emitted = false;
            return new Iterator<Path>() {
                @Override
                public boolean hasNext() {
                    return !emitted;
                }

                @Override
                public Path next() {
                    if (emitted) throw new NoSuchElementException();
                    emitted = true;
                    return new GuestPath("/");
                }
            };
        }
    };
    private final Iterable<FileStore> stores = new Iterable<FileStore>() {
        @Override
        public Iterator<FileStore> iterator() {
            return new Iterator<FileStore>() {
                @Override
                public boolean hasNext() {
                    return false;
                }

                @Override
                public FileStore next() {
                    throw new NoSuchElementException();
                }
            };
        }
    };
    private final Set<String> attributes = new AttributeSet();

    private GuestFileSystem() {
    }

    private static final class AttributeSet implements Set<String> {
        @Override
        public int size() {
            return 1;
        }

        @Override
        public boolean isEmpty() {
            return false;
        }

        @Override
        public boolean contains(Object value) {
            return "basic".equals(value);
        }

        @Override
        public Iterator<String> iterator() {
            return new Iterator<String>() {
                private boolean emitted;

                @Override
                public boolean hasNext() {
                    return !emitted;
                }

                @Override
                public String next() {
                    if (emitted) throw new NoSuchElementException();
                    emitted = true;
                    return "basic";
                }
            };
        }

        @Override
        public Object[] toArray() {
            return new Object[] {"basic"};
        }

        @Override
        public <T> T[] toArray(T[] values) {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean add(String value) {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean remove(Object value) {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean containsAll(Collection<?> values) {
            for (Object value : values) {
                if (!contains(value)) return false;
            }
            return true;
        }

        @Override
        public boolean addAll(Collection<? extends String> values) {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean retainAll(Collection<?> values) {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean removeAll(Collection<?> values) {
            throw new UnsupportedOperationException();
        }

        @Override
        public void clear() {
            throw new UnsupportedOperationException();
        }
    }

    @Override
    public FileSystemProvider provider() {
        return null;
    }

    @Override
    public void close() {

    }

    @Override
    public boolean isOpen() {
        return true;
    }

    @Override
    public boolean isReadOnly() {
        return false;
    }

    @Override
    public String getSeparator() {
        return "/";
    }

    @Override
    public Iterable<Path> getRootDirectories() {
        return roots;
    }

    @Override
    public Iterable<FileStore> getFileStores() {
        return stores;
    }

    @Override
    public Set<String> supportedFileAttributeViews() {
        return attributes;
    }

    @Override
    public Path getPath(String first, String... more) {
        StringBuilder path = new StringBuilder(first);
        for (String part : more) {
            if (path.length() == 0 || path.charAt(path.length() - 1) != '/') {
                path.append('/');
            }
            path.append(part);
        }
        return new GuestPath(path.toString());
    }

    @Override
    public PathMatcher getPathMatcher(String syntaxAndPattern) {
        throw new UnsupportedOperationException(
                "Guest path matcher is not available");
    }

    @Override
    public UserPrincipalLookupService getUserPrincipalLookupService() {
        return null;
    }

    @Override
    public WatchService newWatchService() throws IOException {
        throw new UnsupportedOperationException(
                "Guest watch service is not available");
    }
}
