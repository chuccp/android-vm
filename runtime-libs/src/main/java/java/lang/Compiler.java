package java.lang;

@Deprecated(since="9", forRemoval=true)
public final class Compiler  {
    private Compiler() {}

    public static boolean compileClass(Class<?> clazz) {
        return false;
    }

    public static boolean compileClasses(String string) {
        return false;
    }

    public static Object command(Object any) {
        return null;
    }

    public static void enable() { }

    public static void disable() { }
}
