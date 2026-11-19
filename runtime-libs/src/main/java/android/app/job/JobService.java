package android.app.job;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

/**
 * android.app.job.JobService（AOSP 公开面）。
 *
 * <p>VM 无 JobScheduler 服务端：任务永不派发，onStartJob/onStopJob 由 guest
 * 子类实现（AOSP 抽象面）；onBind 返回 null（AOSP：JobService 不经常规绑定）。
 */
public abstract class JobService extends Service {

    public static final String PERMISSION_BIND = "android.permission.BIND_JOB_SERVICE";

    public abstract boolean onStartJob(JobParameters params);

    public abstract boolean onStopJob(JobParameters params);

    @Override
    public final IBinder onBind(Intent intent) {
        return null;
    }

    public final void jobFinished(JobParameters params, boolean reschedule) {
    }

    public final void jobFinished(JobParameters params, boolean reschedule, int reason) {
    }

    /** AOSP 内部引擎面（guest 一般不直接使用）。 */
    public static final class JobServiceEngine {
        public JobServiceEngine(JobService service) {
        }

        public IBinder getBinder() {
            return null;
        }
    }
}
