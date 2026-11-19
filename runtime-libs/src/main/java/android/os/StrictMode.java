package android.os;

/**
 * StrictMode（AOSP android.os.StrictMode 公开面骨架）。
 * 生成补源：React Native SoLoader 初始化链会构造 ThreadPolicy/VmPolicy
 * （detectAll/penaltyLog/penaltyDeath），类缺失会在会话 pump 中抛
 * NoClassDefFoundError，中断 RN 引擎预初始化。
 */
public final class StrictMode {

    private StrictMode() {
    }

    public static final class ThreadPolicy {
        private ThreadPolicy() {
        }

        public static final class Builder {
            public Builder() {
            }

            public Builder detectAll() {
                return this;
            }

            public Builder detectDiskReads() {
                return this;
            }

            public Builder detectDiskWrites() {
                return this;
            }

            public Builder detectNetwork() {
                return this;
            }

            public Builder detectCustomSlowCalls() {
                return this;
            }

            public Builder permitAll() {
                return this;
            }

            public Builder penaltyLog() {
                return this;
            }

            public Builder penaltyDeath() {
                return this;
            }

            public Builder penaltyDialog() {
                return this;
            }

            public Builder penaltyDropBox() {
                return this;
            }

            public ThreadPolicy build() {
                return new ThreadPolicy();
            }
        }
    }

    public static final class VmPolicy {
        private VmPolicy() {
        }

        public static final class Builder {
            public Builder() {
            }

            public Builder detectAll() {
                return this;
            }

            public Builder detectLeakedSqlLiteObjects() {
                return this;
            }

            public Builder detectLeakedClosableObjects() {
                return this;
            }

            public Builder detectLeakedRegistrationObjects() {
                return this;
            }

            public Builder detectFileUriExposure() {
                return this;
            }

            public Builder penaltyLog() {
                return this;
            }

            public Builder penaltyDeath() {
                return this;
            }

            public Builder penaltyDropBox() {
                return this;
            }

            public VmPolicy build() {
                return new VmPolicy();
            }
        }
    }

    /** AOSP SoLoader 初始化：临时放行磁盘读写（返回旧策略语义）。 */
    public static ThreadPolicy allowThreadDiskWrites() {
        return new ThreadPolicy();
    }

    public static ThreadPolicy allowThreadDiskReads() {
        return new ThreadPolicy();
    }

    public static void setThreadPolicy(ThreadPolicy policy) {
    }

    public static ThreadPolicy getThreadPolicy() {
        return new ThreadPolicy();
    }

    public static void setVmPolicy(VmPolicy policy) {
    }

    public static VmPolicy getVmPolicy() {
        return new VmPolicy();
    }

    public static void enableDefaults() {
    }

    public static void noteSlowCall(String name) {
    }
}
