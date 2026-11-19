package jdk.internal.module;

public class ModuleLoaderMap {
    public static class Mapper
            implements java.util.function.Function<String, ClassLoader> {
        @Override
        public ClassLoader apply(String name) {
            return null;
        }
    }

    public static java.util.Set<String> bootModules() {
        return java.util.Collections.emptySet();
    }

    public static java.util.Set<String> platformModules() {
        return java.util.Collections.emptySet();
    }

}
