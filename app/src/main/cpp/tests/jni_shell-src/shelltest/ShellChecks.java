package shelltest;

import java.util.Arrays;

/**
 * "壳形态" native 库语义自检（判据 = Android/JNI 语义）。每 check* 返回
 * "name:ok" 或 "name:fail:<原因>"。宿主按序调用（checkLoad 需先执行，
 * 其余依赖 register 已绑定）。
 */
public final class ShellChecks {
    private ShellChecks() {}

    private static String shellPath;
    private static boolean loaded;

    private static String ensureReady() {
        if (loaded) return null;
        return "not-loaded";
    }

    public static String checkLoad(String soPath) {
        try {
            ShellLoader.load(soPath);
            ShellNative.register();
            shellPath = soPath;
            loaded = true;
            return "load:ok";
        } catch (Throwable failure) {
            return "load:fail:" + failure.getClass().getSimpleName();
        }
    }

    public static String checkMixed() {
        String err = ensureReady();
        if (err != null) return "mixed:fail:" + err;
        // 期望值 = 与 native 相同的确定性公式（(v ^ 0x5a5a) * 31 + 4919 +
        // ((v * 17) ^ 0x33) + 0x44）；判据是 JNI int 进出无损。
        int expected = ((7 ^ 0x5a5a) * 31 + 4919) + (((7 * 17) ^ 0x33) + 0x44);
        int got = ShellNative.mixed(7);
        return got == expected ? "mixed:ok" : "mixed:fail:" + got;
    }

    public static String checkDecrypted() {
        String err = ensureReady();
        if (err != null) return "decrypted:fail:" + err;
        String got = ShellNative.decrypted();
        return "vm-shell-ok".equals(got) ? "decrypted:ok" : "decrypted:fail:" + got;
    }

    public static String checkTransform() {
        String err = ensureReady();
        if (err != null) return "transform:fail:" + err;
        byte[] source = new byte[] {1, 2, 3, -1, 0x55};
        byte[] expected = new byte[source.length];
        int rolling = 0x5d;
        for (int index = 0; index < source.length; index++) {
            expected[index] = (byte) (source[index] ^ rolling);
            rolling = (rolling * 33 + 0x17 + index) & 0xff;
        }
        byte[] got = ShellNative.transform(source);
        return Arrays.equals(expected, got) ? "transform:ok" : "transform:fail";
    }

    public static String checkNullTransform() {
        String err = ensureReady();
        if (err != null) return "transformNull:fail:" + err;
        try {
            ShellNative.transform(null);
            return "transformNull:fail:no-throw";
        } catch (NullPointerException expected) {
            return "transformNull:ok";
        } catch (Throwable wrong) {
            return "transformNull:fail:" + wrong.getClass().getSimpleName();
        }
    }

    public static String checkFailThrows() {
        String err = ensureReady();
        if (err != null) return "throw:fail:" + err;
        try {
            ShellNative.fail();
            return "throw:fail:no-throw";
        } catch (IllegalArgumentException expected) {
            return "shell-native-failure".equals(expected.getMessage())
                    ? "throw:ok" : "throw:fail:message:" + expected.getMessage();
        } catch (Throwable wrong) {
            return "throw:fail:" + wrong.getClass().getSimpleName();
        }
    }

    public static String checkCallback() {
        String err = ensureReady();
        if (err != null) return "callback:fail:" + err;
        int got = ShellNative.callbackRoundtrip();
        if (got != 0x4f21) return "callback:fail:roundtrip:" + got;
        return ShellNative.callbackValue() == 0x4f21
                ? "callback:ok" : "callback:fail:value:" + ShellNative.callbackValue();
    }

    public static String checkIntegrity() {
        String err = ensureReady();
        if (err != null) return "integrity:fail:" + err;
        if (ShellNative.selfCheck() != 1) return "integrity:fail:selfCheck";
        int expected = ((11 * 17) ^ 0x33) + 0x44;
        int got = ShellNative.integrity();
        return got == expected ? "integrity:ok" : "integrity:fail:" + got;
    }

    public static String checkTracerProbe() {
        String err = ensureReady();
        if (err != null) return "tracerProbe:fail:" + err;
        // TracerPid 探测是"壳会做什么"素材：语义判据 = 探测可执行且返回布尔态
        //（无跟踪器环境为 0；不因宿主平台缺 /proc 而崩溃）。
        int got = ShellNative.tracerProbe();
        return (got == 0 || got == 1) ? "tracerProbe:ok" : "tracerProbe:fail:" + got;
    }
}
