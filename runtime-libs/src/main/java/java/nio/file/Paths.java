package java.nio.file;

import java.nio.file.spi.FileSystemProvider;
import java.net.URI;

public final class Paths {
    private Paths() { }

    public static Path get(String first, String... more) {
        return Path.of(first, more);
    }

    public static Path get(URI uri) {
        return Path.of(uri);
    }
}
