package android.app.job;

import android.content.ComponentName;

/**
 * 后台作业描述（AOSP android.app.job.JobInfo 公开面最小集；VM 无 JobScheduler
 * 服务，实例只作为调用方构造面载体，不会被调度）。
 */
public class JobInfo {
    public static final int NETWORK_TYPE_NONE = 0;
    public static final int NETWORK_TYPE_ANY = 1;
    public static final int NETWORK_TYPE_UNMETERED = 2;
    public static final int NETWORK_TYPE_NOT_ROAMING = 3;

    private final int mJobId;
    private final ComponentName mService;

    JobInfo(int jobId, ComponentName service) {
        this.mJobId = jobId;
        this.mService = service;
    }

    public int getId() {
        return mJobId;
    }

    public ComponentName getService() {
        return mService;
    }

    public static final class Builder {
        private final int mJobId;
        private final ComponentName mService;

        public Builder(int jobId, ComponentName jobService) {
            this.mJobId = jobId;
            this.mService = jobService;
        }

        public Builder setService(ComponentName service) {
            return this;
        }

        public Builder setMinimumLatency(long minLatencyMillis) {
            return this;
        }

        public Builder setOverrideDeadline(long maxExecutionDelayMillis) {
            return this;
        }

        public Builder setPeriodic(long intervalMillis) {
            return this;
        }

        public Builder setRequiredNetworkType(int networkType) {
            return this;
        }

        public Builder setRequiresCharging(boolean requiresCharging) {
            return this;
        }

        public Builder setRequiresDeviceIdle(boolean requiresDeviceIdle) {
            return this;
        }

        public Builder setPersisted(boolean isPersisted) {
            return this;
        }

        public Builder setBackoffCriteria(long initialBackoffMillis, int backoffPolicy) {
            return this;
        }

        public Builder addTriggerContentUri(Object uri) {
            return this;
        }

        public JobInfo build() {
            return new JobInfo(mJobId, mService);
        }
    }
}
