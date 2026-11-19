package android.os;

/**
 * ConditionVariable（AOSP android.os.ConditionVariable 公开面）。
 *
 * 线程间"条件就绪"闩：open/close 置位条件，block 等待条件成立。
 * 语义与 AOSP 一致（synchronized(this) + wait/notifyAll；block 循环防虚假唤醒）。
 * Aegon/Cronet 的 CronetLibraryLoader 静态初始化依赖本类，缺类会让整个
 * Cronet 网络栈 <clinit> 失败（feed 请求因此根本不发出）。
 */
public class ConditionVariable {
    private volatile boolean mCondition;

    public ConditionVariable() {
        mCondition = false;
    }

    public ConditionVariable(boolean state) {
        mCondition = state;
    }

    /** 置位并唤醒所有等待者（幂等；已置位时不重复 notify）。 */
    public void open() {
        synchronized (this) {
            boolean old = mCondition;
            mCondition = true;
            if (!old) {
                notifyAll();
            }
        }
    }

    /** 清除条件；后续 block 将等待下一次 open。 */
    public void close() {
        synchronized (this) {
            mCondition = false;
        }
    }

    /** 阻塞到条件成立（不响应中断，AOSP 同型）。 */
    public void block() {
        synchronized (this) {
            while (!mCondition) {
                try {
                    wait();
                } catch (InterruptedException e) {
                    // AOSP：忽略中断，继续等待条件
                }
            }
        }
    }

    /** 最多等待 timeout 毫秒（0 = 无限）；返回条件是否成立。 */
    public boolean block(long timeout) {
        if (timeout != 0) {
            synchronized (this) {
                long now = System.currentTimeMillis();
                long end = now + timeout;
                while (!mCondition && now < end) {
                    try {
                        wait(end - now);
                    } catch (InterruptedException e) {
                        // AOSP：忽略中断，继续等待
                    }
                    now = System.currentTimeMillis();
                }
                return mCondition;
            }
        }
        block();
        return true;
    }
}
