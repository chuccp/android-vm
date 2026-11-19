package jdk.internal.misc;

public interface JavaObjectInputStreamAccess {
    void checkArray(java.io.ObjectInputStream in, Class<?> arrayClass, int arrayLength)
            throws java.io.IOException;
}
