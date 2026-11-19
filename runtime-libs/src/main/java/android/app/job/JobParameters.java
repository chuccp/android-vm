package android.app.job;

import android.os.PersistableBundle;

/**
 * android.app.job.JobParameters（AOSP 公开数据面，POJO→lib）。
 */
public class JobParameters {
    private final int mJobId;
    private final PersistableBundle mExtras;

    /** @hide AOSP 内部构造面。 */
    public JobParameters(int jobId, PersistableBundle extras) {
        this.mJobId = jobId;
        this.mExtras = extras;
    }

    public int getJobId() {
        return mJobId;
    }

    public PersistableBundle getExtras() {
        return mExtras;
    }
}
