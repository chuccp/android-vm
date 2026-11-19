package apitest;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.Arrays;

/**
 * java.io 文件面语义自检（覆盖面参考启发自 v1 DexFixture.java fileIoFlow/
 * persistentWriteFlow/persistentDeleteFlow/fileThroughputMicros 与
 * GuestFileIoTest.kt 断言面）。判据 = Android java.io 语义：mkdirs/写/追加/
 * length/读回/逐字节相等/listFiles/createNewFile/delete；跨调用持久化由宿主
 * 按顺序调用 write → delete 两式观测。相对路径落点 = 进程 cwd（宿主脚本
 * 切到 scratch 目录执行）。吞吐计时不作断言（语义断言只看数据完整性）。
 */
public final class FileIoChecks {
    private FileIoChecks() {}

    private static final String PERSIST_NAME = "guest-persisted.bin";

    public static String checkFileIoFlow() {
        File directory = new File("io-probe");
        if (!directory.mkdirs() || !directory.isDirectory()) {
            return "fileIo:fail:mkdirs";
        }
        File file = new File(directory, "file-io-probe.bin");
        byte[] first = new byte[64 * 1024];
        byte[] second = new byte[64 * 1024];
        for (int index = 0; index < first.length; index++) {
            first[index] = (byte) (index * 31);
            second[index] = (byte) (index * 17 + 9);
        }
        try {
            FileOutputStream output = new FileOutputStream(file);
            output.write(first);
            output.close();

            output = new FileOutputStream(file, true);
            output.write(second);
            output.close();

            if (file.length() != first.length + second.length) {
                return "fileIo:fail:length";
            }
            byte[] actual = new byte[(int) file.length()];
            FileInputStream input = new FileInputStream(file);
            int offset = 0;
            while (offset < actual.length) {
                int count = input.read(actual, offset, actual.length - offset);
                if (count <= 0) {
                    input.close();
                    return "fileIo:fail:short-read:" + offset;
                }
                offset += count;
            }
            input.close();
            if (offset != actual.length) return "fileIo:fail:short-read";
            if (!Arrays.equals(Arrays.copyOfRange(actual, 0, first.length), first)) {
                return "fileIo:fail:first-block";
            }
            if (!Arrays.equals(
                    Arrays.copyOfRange(actual, first.length, actual.length),
                    second)) {
                return "fileIo:fail:second-block";
            }
            File[] children = directory.listFiles();
            if (children == null || children.length != 1
                    || !"file-io-probe.bin".equals(children[0].getName())) {
                return "fileIo:fail:listFiles";
            }
            File marker = new File(directory, "marker.txt");
            if (!marker.createNewFile() || !marker.exists() || !marker.isFile()) {
                return "fileIo:fail:createNewFile";
            }
            if (!marker.delete()) return "fileIo:fail:delete";
            return "fileIo:ok";
        } catch (Throwable failure) {
            return "fileIo:fail:" + failure.getClass().getSimpleName();
        } finally {
            file.delete();
            directory.delete();
        }
    }

    /** 先调本式，再调 checkPersistentDelete（宿主顺序编排）。 */
    public static String checkPersistentWrite() {
        byte[] expected = new byte[4096];
        for (int index = 0; index < expected.length; index++) {
            expected[index] = (byte) (index * 13 + 5);
        }
        try {
            FileOutputStream output = new FileOutputStream(PERSIST_NAME);
            output.write(expected);
            output.close();
            return "persistentWrite:ok";
        } catch (Throwable failure) {
            return "persistentWrite:fail:" + failure.getClass().getSimpleName();
        }
    }

    public static String checkPersistentDelete() {
        File file = new File(PERSIST_NAME);
        if (!file.isFile() || file.length() != 4096) {
            return "persistentDelete:fail:missing";
        }
        return file.delete() ? "persistentDelete:ok" : "persistentDelete:fail:delete";
    }

    public static String checkFileThroughput() {
        // 16 x 64KiB 写入 + 全量读回（数据完整性判据；计时不作断言）
        File file = new File("file-throughput.bin");
        byte[] chunk = new byte[64 * 1024];
        for (int index = 0; index < chunk.length; index++) {
            chunk[index] = (byte) (index * 29 + 11);
        }
        final long expected = 16L * chunk.length;
        long total = 0;
        try {
            FileOutputStream output = new FileOutputStream(file);
            for (int block = 0; block < 16; block++) {
                output.write(chunk);
            }
            output.close();

            FileInputStream input = new FileInputStream(file);
            byte[] actual = new byte[(int) expected];
            while (total < expected) {
                int count = input.read(actual, (int) total, (int) (expected - total));
                if (count <= 0) {
                    input.close();
                    return "fileThroughput:fail:short:" + total;
                }
                total += count;
            }
            input.close();
            if (total != expected || file.length() != total) {
                return "fileThroughput:fail:size:" + total;
            }
            for (int block = 0; block < 16; block++) {
                if (!Arrays.equals(
                        Arrays.copyOfRange(
                                actual, block * chunk.length, (block + 1) * chunk.length),
                        chunk)) {
                    return "fileThroughput:fail:content:block" + block;
                }
            }
            return "fileThroughput:ok";
        } catch (Throwable failure) {
            return "fileThroughput:fail:" + failure.getClass().getSimpleName();
        } finally {
            file.delete();
        }
    }

    /**
     * read(byte[]) 到 EOF 的返回口径（Android java.io 契约：-1）。
     * 单次越尾读（无循环）；返回 "readEof:observed:<n>" 由宿主按 Android 判据
     * （-1）判定。当前 v2 观测到 0 = FISReadBytes EOF 缺口（builtins_io.cc）。
     */
    public static String checkReadEof() {
        File file = new File("eof-probe.bin");
        try {
            FileOutputStream output = new FileOutputStream(file);
            output.write(7);
            output.close();
            FileInputStream input = new FileInputStream(file);
            byte[] one = new byte[1];
            int first = input.read(one);
            int eof = input.read(one);
            input.close();
            return first == 1
                    ? "readEof:observed:" + eof
                    : "readEof:fail:first:" + first;
        } catch (Throwable failure) {
            return "readEof:fail:" + failure.getClass().getSimpleName();
        } finally {
            file.delete();
        }
    }
}
