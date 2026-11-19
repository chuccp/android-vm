package apitest;

/**
 * 解释器/API 流矩阵自检（对照 docs 14 章夹具矩阵口径扩展）。
 * 覆盖面参考启发自 v1 DexFixture.java（answer/calculate/callStatic/objectFlow/
 * arrayFlow/catchFlow/nestedCatch/nullCatchFlow/classInitTwice/lock/greeting/
 * androidColorFlow）与 NativeVmCoreTest.kt 断言面；测试本体按 v2 guest 考卷
 * 风格（check* 返回 "name:ok" / "name:fail:原因"）+ Android 语义判据重写。
 * v1 host 桥用例（hostBridgeFlow/hostIdentityFlow 一类宿主回调面）不进 v2。
 * 每个 check* 返回 "name:ok" 或 "name:fail:<原因>"。
 */
public final class ApiMatrixChecks {
    private ApiMatrixChecks() {}

    private int value;

    public ApiMatrixChecks(int value) {
        this.value = value;
    }

    public int add(int delta) {
        return value + delta;
    }

    static int counter;

    static {
        counter = 4;
    }

    public static String greeting() {
        return "hello-vm";
    }

    private static int calculate(int left, int right) {
        return (left + right) * 3;
    }

    public static String checkStaticCall() {
        // 无参静态 + 二参静态嵌套调用（invoke-static 链）
        if (calculate(7, 5) != 36) return "staticCall:fail:calc";
        int viaCall = calculate(4, 3);
        if (viaCall != 21) return "staticCall:fail:nested";
        return "staticCall:ok";
    }

    public static String checkObjectFlow() {
        // new + <init> + 实例虚调用（Java 对象语义）
        ApiMatrixChecks fixture = new ApiMatrixChecks(10);
        return fixture.add(5) == 15 ? "objectFlow:ok" : "objectFlow:fail";
    }

    public static String checkArrayFlow() {
        int[] values = new int[2];
        values[0] = 10;
        values[1] = 5;
        if (values.length != 2 || values[0] + values[1] != 15) {
            return "arrayFlow:fail:store";
        }
        try {
            return values[2] == 0 ? "arrayFlow:fail:no-throw" : "arrayFlow:fail";
        } catch (ArrayIndexOutOfBoundsException expected) {
            return "arrayFlow:ok";
        }
    }

    public static String checkCatchFlow() {
        try {
            int denominator = 0;
            return "catchFlow:fail:" + (10 / denominator);
        } catch (ArithmeticException expected) {
            return "catchFlow:ok";
        }
    }

    public static String checkNestedCatch() {
        try {
            try {
                throw new IllegalArgumentException("inner");
            } catch (IllegalStateException wrong) {
                return "nestedCatch:fail:inner-caught";
            }
        } catch (IllegalArgumentException expected) {
            return "nestedCatch:ok";
        }
    }

    public static String checkNullCatch() {
        Object value = null;
        try {
            return "nullCatch:fail:" + value.hashCode();
        } catch (NullPointerException expected) {
            return "nullCatch:ok";
        }
    }

    public static String checkClassInitCount() {
        // <clinit> 只跑一次（counter 初值 4）+ 三次自增观测单调：7 / 10 / 13
        int first = next();
        int second = next();
        int third = next();
        return (first == 7 && second == 10 && third == 13)
                ? "classInitCount:ok"
                : "classInitCount:fail:" + first + "," + second + "," + third;
    }

    private static int next() {
        counter += 3;
        return counter;
    }

    public static String checkLockHold() {
        // ReentrantLock 可重入持有计数（java.util.concurrent.locks 语义）
        java.util.concurrent.locks.ReentrantLock lock =
                new java.util.concurrent.locks.ReentrantLock();
        lock.lock();
        try {
            lock.lock();
            try {
                return lock.getHoldCount() == 2 ? "lockHold:ok" : "lockHold:fail";
            } finally {
                lock.unlock();
            }
        } finally {
            lock.unlock();
        }
    }

    public static String checkColorRgb() {
        // android.graphics.Color.rgb（runtime.dex android-core-values 面，Android 语义）
        int rgb = android.graphics.Color.rgb(1, 2, 3);
        return rgb == 0xff010203 ? "colorRgb:ok" : "colorRgb:fail:" + rgb;
    }
}
