package com.coke.vm;

/**
 * R8 加固 .so 加载 fixture（真实 app 路径）：static 块 System.loadLibrary——
 * payload 先于 fixture（jni_shell 先例；fixture DT_NEEDED 直链 payload，链接器
 * 带依赖解析）。加载链路：guest System.loadLibrary -> VM LoadLibraryCommon ->
 * 设备 LibraryLoadHook -> APK lib/&lt;abi&gt; 提取 -&gt; dlopen -> JNI_OnLoad
 * （构造 cookie/TracerPid 路径/dladdr/版本协商 JNI_VERSION_1_6）。
 * ensureLoaded() 仅触发类初始化（加载失败 = ExceptionInInitializerError 上抛）。
 */
public final class HardenedNativeLoader {
    static {
        System.loadLibrary("hardened_payload");
        System.loadLibrary("hardened_fixture");
    }

    private HardenedNativeLoader() {
    }

    /** 触发 <clinit>（加载 .so）；幂等。 */
    public static void ensureLoaded() {
    }

    /** 加载 + RegisterNatives 入口表绑定（Java_com_coke_vm_..._register）。 */
    public static int loadAndRegister() {
        HardenedNativeFixture.register();
        return 580;
    }
}
