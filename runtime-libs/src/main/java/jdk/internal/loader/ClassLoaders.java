package jdk.internal.loader;

// 生成补源（sun-compat）：jdk.internal.loader.ClassLoaders（platform/app loader 分层）。
// 本 VM 无 platform/app loader 分层（classpath 语义）：platformClassLoader() 恒 null
// （ServiceLoader LazyClassPathLookupIterator 的 platform 分支不可达，全部走
// loader.getResources 资源面）。
public final class ClassLoaders {

    private ClassLoaders() {
    }

    public static ClassLoader platformClassLoader() {
        return null;
    }

    public static ClassLoader appClassLoader() {
        return null;
    }
}
