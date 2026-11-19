package java.lang;

import java.io.File;

class ClassLoaderHelper {

    private ClassLoaderHelper() {}

    static final boolean allowsQuotedPathElements = false;

    static boolean loadLibraryOnlyIfPresent() {
        return true;
    }

    static File mapAlternativeName(File lib) {
        return null;
    }
}
