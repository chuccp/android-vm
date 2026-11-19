package android.app.job;

import java.util.ArrayList;
import java.util.List;

/**
 * 后台作业调度服务（AOSP android.app.job.JobScheduler 公开面；VM 无
 * JobScheduler 服务，getSystemService 返回 null——本类仅保证类型链接面）。
 * 若调用方持有实例（不会发生），调度调用为本地 no-op。
 */
public class JobScheduler {
    public static final int RESULT_SUCCESS = 0;
    public static final int RESULT_FAILURE = 1;

    public int schedule(JobInfo job) {
        return RESULT_SUCCESS;
    }

    public void cancel(int jobId) {
    }

    public int cancelAll() {
        return 0;
    }

    public List<JobInfo> getAllPendingJobs() {
        return new ArrayList<>();
    }

    public JobInfo getPendingJob(int jobId) {
        return null;
    }
}
