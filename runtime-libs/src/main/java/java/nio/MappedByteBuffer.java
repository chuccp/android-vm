package java.nio;

import java.io.FileDescriptor;
import java.lang.ref.Reference;
import jdk.internal.misc.Unsafe;

public abstract class MappedByteBuffer
    extends ByteBuffer
{

    private final FileDescriptor fd;

    MappedByteBuffer(int mark, int pos, int lim, int cap,
                     FileDescriptor fd)
    {
        super(mark, pos, lim, cap, null, 0, null);
        this.fd = fd;
    }

    MappedByteBuffer(int mark, int pos, int lim, int cap) {
        super(mark, pos, lim, cap, null, 0, null);
        this.fd = null;
    }

    private long mappingOffset() {
        int ps = Bits.pageSize();
        long offset = address % ps;
        return (offset >= 0) ? offset : (ps + offset);
    }

    private long mappingAddress(long mappingOffset) {
        return address - mappingOffset;
    }

    private long mappingLength(long mappingOffset) {
        return (long)capacity() + mappingOffset;
    }

    public final boolean isLoaded() {
        if (fd == null) {
            return true;
        }
        if ((address == 0) || (capacity() == 0))
            return true;
        long offset = mappingOffset();
        long length = mappingLength(offset);
        return isLoaded0(mappingAddress(offset), length, Bits.pageCount(length));
    }

    private static byte unused;

    public final MappedByteBuffer load() {
        if (fd == null) {
            return this;
        }
        if ((address == 0) || (capacity() == 0))
            return this;
        long offset = mappingOffset();
        long length = mappingLength(offset);
        load0(mappingAddress(offset), length);

        Unsafe unsafe = Unsafe.getUnsafe();
        int ps = Bits.pageSize();
        int count = Bits.pageCount(length);
        long a = mappingAddress(offset);
        byte x = 0;
        try {
            for (int i=0; i<count; i++) {

                x ^= unsafe.getByte(a);
                a += ps;
            }
        } finally {
            Reference.reachabilityFence(this);
        }
        if (unused != 0)
            unused = x;

        return this;
    }

    public final MappedByteBuffer force() {
        if (fd == null) {
            return this;
        }
        if ((address != 0) && (capacity() != 0)) {
            long offset = mappingOffset();
            force0(fd, mappingAddress(offset), mappingLength(offset));
        }
        return this;
    }

    private native boolean isLoaded0(long address, long length, int pageCount);
    private native void load0(long address, long length);
    private native void force0(FileDescriptor fd, long address, long length);

    // position/limit/mark/reset/clear/flip/rewind 不做协变返回覆写（Android
    // API 形态即继承 Buffer 返回型；调用点描述符为 (I)Ljava/nio/Buffer; 等基类
    // 形）。协变覆写会让 javac 生成 bridge 合成方法——本工具链（javac ×
    // --patch-module）产出的 bridge 体自调用 bridge 描述符（递归炸弹），
    // 且 Android API 亦未协变，故按基类面承接。
}
