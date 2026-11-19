package sun.nio.cs;

/** JDK 数组解码快速面（StringCoding instanceof 探测；本 VM 无实现者即走慢路径）。 */
public interface ArrayDecoder {

    int decode(byte[] src, int off, int len, char[] dst);

    boolean isASCIICompatible();

    boolean isLatin1Decodable();

    int decodeToLatin1(byte[] src, int off, int len, byte[] dst);
}
