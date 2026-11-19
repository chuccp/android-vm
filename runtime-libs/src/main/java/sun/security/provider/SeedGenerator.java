package sun.security.provider;

import java.io.FileInputStream;
import java.io.IOException;

/**
 * 种子源（§31.7 C 档 lib：OpenJDK 公开面形态）。
 *
 * <p>熵源 = {@code /dev/urandom}，读取走 lib java.io.FileInputStream
 * （java.io 文件面；FileInputStream/FileDescriptor native 缝由 VM 承接）。
 * 消费口：{@link #generateSeed(int)}（SHA1PRNG engineGenerateSeed /
 * 无种启动熵补）。
 */
final class SeedGenerator {

    private static final String DEVICE = "/dev/urandom";

    private SeedGenerator() {
    }

    static byte[] generateSeed(int numBytes) {
        if (numBytes < 0) {
            throw new IllegalArgumentException("numBytes cannot be negative");
        }
        byte[] result = new byte[numBytes];
        generateSeed(result);
        return result;
    }

    static void generateSeed(byte[] result) {
        FileInputStream in = null;
        try {
            in = new FileInputStream(DEVICE);
            int off = 0;
            while (off < result.length) {
                int n = in.read(result, off, result.length - off);
                if (n < 0) {
                    throw new IOException("Unexpected EOF reading " + DEVICE);
                }
                off += n;
            }
        } catch (IOException e) {
            throw new RuntimeException("SeedGenerator: " + DEVICE + " read failed", e);
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (IOException ignore) {
                }
            }
        }
    }
}
