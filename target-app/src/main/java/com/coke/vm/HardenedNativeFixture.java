package com.coke.vm;

/**
 * R8 加固 .so JNI 绑定面（target-app 考卷收官轮）。
 *
 * 绑定 target-app 的 libhardened_fixture.so（预期值见 v1 GuestNativeLibraryTest.kt
 * / tests/jni_shell 壳形态同源手法）：静态导出 a/b/c + register() 走
 * RegisterNatives 注册 a-g 共 7 个方法。加载走真实 app 路径：
 * {@link HardenedNativeLoader} 的 static 块 System.loadLibrary（payload 先于
 * fixture，同 jni_shell 先例；fixture DT_NEEDED 直链 payload）。
 *
 * probe* 方法 = 哨兵契约（全对返回 581..587，失败返回缺口码）；JNI 语义
 * 判据按 Android/JNI 文档，非 fixture 特判。
 */
public final class HardenedNativeFixture {
    private static volatile int callbackValue;
    // a/b/c 计数镜像（probe 面自洽：c() 键 = count ^ (pid ^ tracer)）
    private static volatile int nativeCalls;

    private HardenedNativeFixture() {
    }

    /** JNI 入口表绑定（Java_* 导出符号解析；register 内 RegisterNatives）。 */
    public static native void register();

    /** cookie 校验 + 跨库混合运算。 */
    public static native int a(int value);

    /** Rolling-XOR 解密文本（NewStringUTF 语义面）。 */
    public static native String b();

    /** dladdr 自省 + getpid 键（跨库活性）。 */
    public static native int c();

    /** byte[] Rolling-XOR 变换 + 空指针 ThrowNew（Get/SetByteArrayRegion 面）。 */
    public static native byte[] d(byte[] input);

    /** ThrowNew(IllegalArgumentException, "hardened-native-failure")。 */
    public static native void e();

    /** 跨库完整性（cookie + hp_alive_7f + dladdr）。 */
    public static native int f();

    /** JNI 上调 Java 静态回调（0x4f21）。 */
    public static native int g();

    public static int callbackValue() {
        return callbackValue;
    }

    public static void onNativeCallback(int value) {
        callbackValue = value;
    }

    // ---- 哨兵探针（R8 断言面；581..587=通过）----

    /** a(7) == (7^0x5a5a)*31+4919 + hp_mix_7f(7) = 722178 -> 581。 */
    public static int probeMixed() {
        HardenedNativeLoader.ensureLoaded();
        int expected = ((7 ^ 0x5a5a) * 31 + 4919) + (((7 * 17) ^ 0x33) + 0x44);
        nativeCalls++;
        return a(7) == expected ? 581 : 0;
    }

    /** b() == "vm-hardened-so" -> 582。 */
    public static int probeDecrypted() {
        HardenedNativeLoader.ensureLoaded();
        nativeCalls++;
        return "vm-hardened-so".equals(b()) ? 582 : 0;
    }

    /** c() 有效（非 -1/-2）且计数键一致（c() == count ^ getpid()^(TracerPid?1:0)）-> 583。 */
    public static int probeSelfIntegrity() {
        HardenedNativeLoader.ensureLoaded();
        // g_fixture_calls 仅 a/b 自增（c/d/e/f/g 不自增）；nativeCalls 为镜像。
        // c() 键 = g_fixture_calls ^ RuntimeKey，RuntimeKey = pid ^ (tracer?1:0)。
        int base = nativeCalls;
        int first = c();
        int second = c();
        if (first < 0 || second < 0) {
            return -1;
        }
        if (first != second) {
            return -2;  // c() 不自增：连续两次读数必须一致
        }
        int pid = android.os.Process.myPid();
        return (first == (base ^ pid) || first == (base ^ (pid ^ 1))) ? 583 : -3;
    }

    /** d() 滚动 XOR 数组拷贝往返 + null -> NullPointerException -> 584。 */
    public static int probeArray() {
        HardenedNativeLoader.ensureLoaded();
        HardenedNativeLoader.loadAndRegister();
        byte[] source = new byte[] {1, 2, 3, 4, 5};
        byte[] expected = new byte[source.length];
        byte rolling = 0x5d;
        for (int index = 0; index < source.length; index++) {
            expected[index] = (byte) (source[index] ^ rolling);
            rolling = (byte) (rolling * 33 + 0x17 + index);
        }
        byte[] got = d(source);
        if (got == null || got.length != expected.length) {
            return -1;
        }
        for (int index = 0; index < expected.length; index++) {
            if (got[index] != expected[index]) {
                return -2;
            }
        }
        try {
            d(null);
            return -3;
        } catch (NullPointerException expectedException) {
            return "hardened byte array is null".equals(expectedException.getMessage()) ? 584 : -4;
        }
    }

    /** e() ThrowNew(IllegalArgumentException, "hardened-native-failure") -> 585。 */
    public static int probeThrow() {
        HardenedNativeLoader.ensureLoaded();
        HardenedNativeLoader.loadAndRegister();
        try {
            e();
            return -1;
        } catch (IllegalArgumentException expectedException) {
            return "hardened-native-failure".equals(expectedException.getMessage()) ? 585 : -2;
        }
    }

    /** f() == hp_mix_7f(11) = 204（跨库完整性）-> 586。 */
    public static int probeIntegrity() {
        HardenedNativeLoader.ensureLoaded();
        HardenedNativeLoader.loadAndRegister();
        return f() == (((11 * 17) ^ 0x33) + 0x44) ? 586 : 0;
    }

    /** g() == 0x4f21 且回调观察值一致 -> 587。 */
    public static int probeCallback() {
        HardenedNativeLoader.ensureLoaded();
        HardenedNativeLoader.loadAndRegister();
        int got = g();
        return got == 0x4f21 && callbackValue == 0x4f21 ? 587 : 0;
    }
}
