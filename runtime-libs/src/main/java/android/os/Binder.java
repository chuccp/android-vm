package android.os;

/**
 * binder 实体（AOSP android.os.Binder 公开面；§31.7 C 档 lib 骨架——binder 缝
 * 归桥接层；本面接口描述/身份态）。
 */
public class Binder implements IBinder {

    private String mDescriptor;
    private IInterface mOwner;

    public Binder() {
    }

    public static IBinder getCallingBinder() {
        return null;
    }

    public static int getCallingPid() {
        return android.os.Process.myPid();
    }

    public static int getCallingUid() {
        return 0;
    }

    public static final boolean isProxy() {
        return false;
    }

    public static void joinThreadPool() {
    }

    public void attachInterface(IInterface owner, String descriptor) {
        this.mOwner = owner;
        this.mDescriptor = descriptor;
    }

    @Override
    public String getInterfaceDescriptor() {
        return this.mDescriptor;
    }

    @Override
    public boolean pingBinder() {
        return true;
    }

    @Override
    public boolean isBinderAlive() {
        return true;
    }

    @Override
    public IInterface queryLocalInterface(String descriptor) {
        // AOSP 同进程语义：descriptor 命中则返回本地接口，AIDL Stub.asInterface
        // 据此走直调（不走 Parcel transact）。VM 的 bindService 为同进程本地
        // 服务，此路径即 binder 缝的本地短路。
        if (mDescriptor != null && mDescriptor.equals(descriptor)) {
            return mOwner;
        }
        return null;
    }

    @Override
    public void dump(java.io.FileDescriptor fd, String[] args) {
    }

    @Override
    public void dumpAsync(java.io.FileDescriptor fd, String[] args) {
    }

    @Override
    public boolean transact(int code, Parcel data, Parcel reply, int flags) {
        // binder 线程池语义投影：onTransact 在独立 GuestTask（1:1 线程语义）
        // 上执行，caller join 到完成、结果回填 reply（AOSP 同进程 binder 派发面；
        // 线程池复用后置）。
        final boolean[] result = new boolean[1];
        Thread worker = new Thread(new Runnable() {
            @Override
            public void run() {
                result[0] = Binder.this.onTransact(code, data, reply, flags);
            }
        }, "Binder_" + code);
        worker.start();
        try {
            worker.join();
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            return false;
        }
        return result[0];
    }

    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
        return false;
    }

    @Override
    public void linkToDeath(DeathRecipient recipient, int flags) {
    }

    @Override
    public boolean unlinkToDeath(DeathRecipient recipient, int flags) {
        return true;
    }

    public void dump(java.io.FileDescriptor fd, java.io.PrintWriter fout, String[] args) {
    }
}
