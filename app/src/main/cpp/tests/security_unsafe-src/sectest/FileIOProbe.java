package sectest;

import java.io.FileInputStream;
import java.io.IOException;

/**
 * java.io 文件面探针（FileInputStream/FileDescriptor native 缝 POSIX 直通；
 * /dev/urandom = SeedGenerator 熵源同路径）。覆盖：读块/读字节/skip/
 * available/显式 close + 未命中 FileNotFoundException。
 */
public class FileIOProbe {

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();
        FileInputStream in = null;
        try {
            in = new FileInputStream("/dev/urandom");
            byte[] buf = new byte[16];
            int off = 0;
            while (off < 16) {
                int k = in.read(buf, off, 16 - off);
                if (k < 0) {
                    break;
                }
                off += k;
            }
            sb.append(check(off == 16, "fis-read-bytes"));
            int one = in.read();
            sb.append(check(one >= 0 && one <= 255, "fis-read-byte"));
            long skipped = in.skip(4);
            sb.append(check(skipped == 4, "fis-skip"));
            sb.append(check(in.available() >= 0, "fis-available"));
        } catch (Throwable t) {
            sb.append("fis-urandom: FAIL ").append(t).append('\n');
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (IOException ignore) {
                }
            }
        }
        try {
            FileInputStream missing = new FileInputStream("/definitely/missing/urandom");
            missing.close();
            sb.append("fis-missing: FAIL (no exception)\n");
        } catch (java.io.FileNotFoundException expected) {
            sb.append("fis-missing: OK\n");
        } catch (Throwable t) {
            sb.append("fis-missing: FAIL ").append(t).append('\n');
        }
        return sb.toString();
    }
}
