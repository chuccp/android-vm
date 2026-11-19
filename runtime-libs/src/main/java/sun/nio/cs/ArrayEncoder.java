package sun.nio.cs;

/** JDK 数组编码快速面（StringCoding instanceof 探测；本 VM 无实现者即走慢路径）。 */
public interface ArrayEncoder {

    int encode(char[] src, int off, int len, byte[] dst);

    boolean isASCIICompatible();

    int encodeFromLatin1(byte[] src, int sp, int len, byte[] dst);

    int encodeFromUTF16(byte[] src, int sp, int len, byte[] dst);
}
