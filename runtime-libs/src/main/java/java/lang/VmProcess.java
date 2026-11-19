package java.lang;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * VM 哑进程：自研运行时无外部进程能力（Android 沙箱 + 解释器宿主），
 * {@link ProcessBuilder#start()} 返回本类实例——空输出、立即退出，
 * 调用方按"命令无输出"降级（设备信息采集链等只读 stdout）。
 */
public final class VmProcess extends Process {
    private static final InputStream EMPTY_IN = new ByteArrayInputStream(new byte[0]);

    private final OutputStream mOut = new ByteArrayOutputStream();
    private volatile boolean mDestroyed;

    public VmProcess() {
    }

    @Override
    public OutputStream getOutputStream() {
        return mOut;
    }

    @Override
    public InputStream getInputStream() {
        return EMPTY_IN;
    }

    @Override
    public InputStream getErrorStream() {
        return EMPTY_IN;
    }

    @Override
    public synchronized int waitFor() {
        return 0;
    }

    @Override
    public synchronized int exitValue() {
        return 0;
    }

    @Override
    public void destroy() {
        mDestroyed = true;
    }

    @Override
    public boolean isAlive() {
        return false;
    }
}
