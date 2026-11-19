package shelltest;

/**
 * "壳形态" native 库语义面（覆盖面参考启发自 v1 hardened_fixture.cpp /
 * HardenedNativeFixture.java / GuestNativeLibraryTest.kt 的对抗手法清单：
 * 构造 cookie、TracerPid 探测、Rolling-XOR 变换、ThrowNew、RegisterNatives、
 * CallStaticVoidMethod 回调、dladdr 完整性——手法作语义素材，断言按
 * Android/JNI 语义判据）。
 * native 方法先经 Java_* 导出符号解析（register），register 内 RegisterNatives
 * 绑定其余方法（真实壳加载链路形态）。
 */
public final class ShellNative {
    private static volatile int callbackValue;

    private ShellNative() {
    }

    /** JNI 入口表绑定（register 内 RegisterNatives）。 */
    public static native void register();

    /** cookie 校验 + 确定性混合运算。 */
    public static native int mixed(int value);

    /** Rolling-XOR 解密文本（NewStringUTF 语义面）。 */
    public static native String decrypted();

    /** 构造 cookie + 跨库存活 + dladdr 解析自检（1=ok）。 */
    public static native int selfCheck();

    /** byte[] Rolling-XOR 变换（Get/SetByteArrayRegion 语义面）。 */
    public static native byte[] transform(byte[] input);

    /** ThrowNew(IllegalArgumentException) 语义面。 */
    public static native void fail();

    /** 跨库调用确定性值（dladdr 完整性通过后返回）。 */
    public static native int integrity();

    /** CallStaticVoidMethod 回调（0x4f21 观测值）。 */
    public static native int callbackRoundtrip();

    /** TracerPid 反调试探测素材（0/1，观测值不作强判据）。 */
    public static native int tracerProbe();

    public static int callbackValue() {
        return callbackValue;
    }

    public static void onNativeCallback(int value) {
        callbackValue = value;
    }
}
