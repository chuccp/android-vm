package java.nio.channels;

/**
 * 重叠文件锁异常（java.nio.channels.OverlappingFileLockException；
 * FileChannel 锁面链接类型，VM 内文件通道为本地实现，不会实际抛出）。
 */
public class OverlappingFileLockException extends IllegalStateException {
    private static final long serialVersionUID = 1L;

    public OverlappingFileLockException() {
        super();
    }
}
