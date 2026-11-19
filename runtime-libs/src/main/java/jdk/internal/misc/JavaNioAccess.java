package jdk.internal.misc;

public interface JavaNioAccess {
    interface BufferPool {
        String getName();
        long getCount();
        long getTotalCapacity();
        long getMemoryUsed();
    }

    BufferPool getDirectBufferPool();
    java.nio.ByteBuffer newDirectByteBuffer(long addr, int cap, Object ob);
    void truncate(java.nio.Buffer buf);
}
