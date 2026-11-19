// 宿主 JavaVM 句柄（native -> 宿主 Java 桥的入口）。
//
// libdexvm.so 由宿主 Android app 加载；VM 工作线程是宿主 Java 线程
// （MainActivity "dexvm-open"）。native 侧把宿主字体度量/Typeface 创建
// 委托给 com.dexvm.host.FontBridge（真 Android Paint/Typeface），这里
// 保存真实 JavaVM*，按线程 GetEnv/AttachCurrentThread。
#pragma once

namespace dexvm::runtime {

void SetHostJavaVM(void* vm);  // 真实 JavaVM*（JNI_OnLoad/入口 env->GetJavaVM）
void* HostJavaVM();

}  // namespace dexvm::runtime
