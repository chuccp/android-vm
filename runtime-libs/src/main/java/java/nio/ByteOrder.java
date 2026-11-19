package java.nio;

import jdk.internal.misc.Unsafe;

public final class ByteOrder {

    private String name;

    private ByteOrder(String name) {
        this.name = name;
    }

    public static final ByteOrder BIG_ENDIAN
        = new ByteOrder("BIG_ENDIAN");

    public static final ByteOrder LITTLE_ENDIAN
        = new ByteOrder("LITTLE_ENDIAN");

    private static final ByteOrder NATIVE_ORDER
        = Unsafe.getUnsafe().isBigEndian()
            ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN;

    public static ByteOrder nativeOrder() {
        return NATIVE_ORDER;
    }

    public String toString() {
        return name;
    }


    // ---- overlay：isBigEndian（JBR 25 精简面）----
    public boolean isBigEndian() {
        return this == BIG_ENDIAN;
    }

}
