package jdk.internal.misc;

public interface JavaUtilZipFileAccess {
    int getManifestNum(java.util.jar.JarFile jar);
    String[] getMetaInfEntryNames(java.util.zip.ZipFile zip);
    <T extends java.util.zip.ZipEntry> java.util.Enumeration<T> entries(
            java.util.jar.JarFile jar,
            java.util.function.Function<String, T> fn);
    <T extends java.util.zip.ZipEntry> java.util.stream.Stream<T> stream(
            java.util.jar.JarFile jar,
            java.util.function.Function<String, T> fn);
    java.util.stream.Stream<String> entryNameStream(java.util.jar.JarFile jar);
    java.util.zip.ZipEntry getEntry(
            java.util.zip.ZipFile zip, String name,
            java.util.function.Function<String, ? extends java.util.zip.ZipEntry> fn);
}
