// JNI contract：libdexvm.so 对 App 唯一导出面（docs 01 章 §3）
//
// 注意：本 TU 用 "jni/jni_env.h"（guest JNI 全面）替代系统 <jni.h>
// （jni_env.h 与 <jni.h> 同 TU 互斥）。ABI 面与真实 jni.h 完整布局兼容：
// 宿主 ART 传入的真实 JNIEnv*/JavaVM* 按 _JNIEnv/_JavaVM 首成员 functions
// 指针位型对接（tools/jni_full_tests.sh 槽位校验），故宿主 contract 面与
// guest JNI 面可在同一 TU 共存；C 调用风格 env->functions->X(env, ...)。
#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/ucontext.h>  // ucontext_t/寄存器位型（<ucontext.h> 在 macOS 需 _XOPEN_SOURCE）
#include <unistd.h>        // access/unlink（设备验证探针 marker）

#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>
#if defined(__ANDROID__)
#include <android/log.h>

#include <atomic>
#include <chrono>
#include <thread>
#endif

#include "bridge/bridge.h"
#include "bridge/ui_bridge_input.h"
#include "bridge/ui_bridge_protocol.h"
#include "core/boot_network.h"
#include "core/builtins.h"
#include "core/font_worker.h"
#include "core/host_java.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/framework_builtins.h"
#include "core/framework_dialog.h"
#include "core/gc.h"
#include "core/interpreter.h"
#include "core/intrinsic.h"
#include "core/notification_bridge.h"
#include "core/toast_bridge.h"
#include "core/sensor_bridge.h"
#include "core/tls_bridge.h"
#include "dex/dex_file.h"
#include "framework/lifecycle_driver.h"
#include "framework/render.h"
#include "jni/jni_env.h"
#include "jni/native_manager.h"
#include "memory/heap.h"
#include "session/class_loader.h"
#include "session/session.h"
#include "tests/target_exam_table.h"

#ifndef JNIEXPORT
#define JNIEXPORT __attribute__((visibility("default")))
#endif

// core/framework_view.cc 实现体（会话帧每轮异步布局后派发 observer 面；
// requestLayout/invalidate 脏位驱动下一帧 traversal）。
namespace dexvm::runtime {
void DispatchViewTreeGlobalLayout(VMState& state, VObject* root);
bool DispatchViewTreePreDraw(VMState& state, VObject* root);
bool TakeGuestLayoutRequest();
}

namespace {

constexpr const char* kClassName = "com/dexvm/host/NativeBridge";

// 首帧窗口（M4.2 固定 guest 窗口；窗口尺寸链后置）。默认 540x960；
// 宿主可按内容区宽高比在启动前通过 nativeSetGuestWindowSize 调整高度，
// 使物化画面的 x/y 缩放系数一致（避免文字被纵向拉伸）。
constexpr int32_t kGuestWindowWidth = 540;
constexpr int32_t kGuestWindowHeight = 960;
std::atomic<int32_t> g_guest_window_width{kGuestWindowWidth};
std::atomic<int32_t> g_guest_window_height{kGuestWindowHeight};

// ---- 运行进度日志（设备 UI 实时显示启动/考卷日志；线程安全快照）----
std::mutex g_progress_mutex;
std::string g_progress_log;

void ProgressAppend(const std::string& line) {
  {
    std::lock_guard<std::mutex> lock(g_progress_mutex);
    if (g_progress_log.size() > 192 * 1024) {
      g_progress_log.erase(0, g_progress_log.size() - 96 * 1024);
    }
    g_progress_log += line;
    g_progress_log += '\n';
  }
#if defined(__ANDROID__)
  // 设备诊断面：进度日志同落 logcat（会话/点击/缺口归因）
  __android_log_print(ANDROID_LOG_INFO, "DexVmProgress", "%s", line.c_str());
#endif
}

void ProgressClear() {
  std::lock_guard<std::mutex> lock(g_progress_mutex);
  g_progress_log.clear();
}

std::string ProgressSnapshot() {
  std::lock_guard<std::mutex> lock(g_progress_mutex);
  return g_progress_log;
}

// 生命周期阶段 -> 进度行（NativeRunApkLifecycle / NativeRunExam /
// NativeRunTargetExam 共用观测器）
void AttachProgressObserver(dexvm::framework::LifecycleDriver& driver) {
  driver.set_observer([](const std::string& phase, bool begin,
                         const dexvm::framework::PhaseRecord* rec) {
    if (begin || rec == nullptr) return;
    if (rec->ok) {
      ProgressAppend("phase " + phase + " ok (" + std::to_string(rec->elapsed_ms) + " ms)");
    } else {
      ProgressAppend("phase " + phase + " FAIL: " + rec->error);
    }
  });
}

jstring NativeVmVersion(JNIEnv* env, jclass) {
  return env->functions->NewStringUTF(env, "dexvm-m1");
}

// 运行中进度日志快照（UI 轮询；guest/考卷线程写入，互斥保护）
// 宿主桥接值面：宿主 Java 推入系统服务真值（k=v 行编码）-> Host.get 读取
void NativeSetHostValues(JNIEnv* env, jclass, jstring encoded) {
  if (encoded == nullptr) return;
  const char* text = env->functions->GetStringUTFChars(env, encoded, nullptr);
  if (text == nullptr) return;
  dexvm::runtime::ClearHostValues();
  std::string all(text);
  size_t start = 0;
  while (start <= all.size()) {
    size_t nl = all.find('\n', start);
    std::string line = all.substr(start, nl == std::string::npos ? std::string::npos
                                                                 : nl - start);
    size_t eq = line.find('=');
    if (eq != std::string::npos) {
      dexvm::runtime::SetHostValue(line.substr(0, eq), line.substr(eq + 1));
    }
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  env->functions->ReleaseStringUTFChars(env, encoded, text);
}

// 宿主传感器桥：SensorBridge.java 推入传感器描述表与实时事件。
// 仅入 VM 侧互斥队列（不触 guest 堆——宿主回调线程安全）。
void NativeSensorSetList(JNIEnv* env, jclass, jstring encoded) {
  if (encoded == nullptr) return;
  const char* text = env->functions->GetStringUTFChars(env, encoded, nullptr);
  if (text == nullptr) return;
  dexvm::runtime::SetHostSensorList(std::string(text));
  env->functions->ReleaseStringUTFChars(env, encoded, text);
}

void NativeSensorPush(JNIEnv* env, jclass, jint type, jfloatArray values, jlong timestamp) {
  float buf[16] = {};
  jsize len = 0;
  if (values != nullptr) {
    len = env->functions->GetArrayLength(env, values);
    if (len > static_cast<jsize>(sizeof(buf) / sizeof(buf[0]))) {
      len = static_cast<jsize>(sizeof(buf) / sizeof(buf[0]));
    }
    if (len > 0) {
      env->functions->GetFloatArrayRegion(env, values, 0, len, buf);
    }
  }
  dexvm::runtime::PushHostSensorEvent(static_cast<int32_t>(type), buf,
                                      static_cast<int32_t>(len),
                                      static_cast<int64_t>(timestamp));
}

// 宿主通知桥：NotificationBridge.java 分发线程阻塞取一条请求（200ms 超时返 null）。
jbyteArray NativeNotificationTake(JNIEnv* env, jclass) {
  dexvm::runtime::HostNotificationRequest req;
  if (!dexvm::runtime::TakeHostNotificationRequest(&req, 200)) return nullptr;
  std::string bytes = dexvm::runtime::EncodeHostNotificationRequest(req);
  jbyteArray arr = env->functions->NewByteArray(env, static_cast<jsize>(bytes.size()));
  if (arr != nullptr) {
    env->functions->SetByteArrayRegion(env, arr, 0, static_cast<jsize>(bytes.size()),
                                       reinterpret_cast<const jbyte*>(bytes.data()));
  }
  return arr;
}

// 宿主 Toast 桥：ToastBridge.java 分发线程阻塞取一条请求（200ms 超时返 null）。
jbyteArray NativeToastTake(JNIEnv* env, jclass) {
  dexvm::runtime::HostToastRequest req;
  if (!dexvm::runtime::TakeHostToastRequest(&req, 200)) return nullptr;
  std::string bytes = dexvm::runtime::EncodeHostToastRequest(req);
  jbyteArray arr = env->functions->NewByteArray(env, static_cast<jsize>(bytes.size()));
  if (arr != nullptr) {
    env->functions->SetByteArrayRegion(env, arr, 0, static_cast<jsize>(bytes.size()),
                                       reinterpret_cast<const jbyte*>(bytes.data()));
  }
  return arr;
}

jstring NativeGetRunProgress(JNIEnv* env, jclass) {
  std::string snapshot = ProgressSnapshot();
  return env->functions->NewStringUTF(env, snapshot.c_str());
}

void NativeClearRunProgress(JNIEnv* env, jclass) {
  (void)env;
  ProgressClear();
}

// M1 验收入口：引导 VM（heap/classlinker/interpreter）并执行 fixture DEX。
jstring NativeRunFixture(JNIEnv* env, jclass, jstring dex_path) {
  const char* path = env->functions->GetStringUTFChars(env, dex_path, nullptr);
  std::string result;
  std::string error;

  do {
    // S1：打开 DEX/APK（docs 03 章 §10 Session）
    auto session = dexvm::session::Session::Open(path, &error);
    if (session == nullptr) {
      result = "FAIL open: " + error;
      break;
    }

    // 引导 VM（docs 03 章 §7 精简）
    static auto heap = std::make_unique<dexvm::mem::Heap>();
    static auto intern = std::make_unique<dexvm::runtime::InternTable>();
    auto linker = std::make_unique<dexvm::runtime::ClassLinker>(heap.get(), intern.get());
    dexvm::runtime::RegisterBuiltins(linker.get());
    dexvm::runtime::VMState state;
    state.heap = heap.get();
    state.intern = intern.get();
    state.linker = linker.get();
    // 自动 GC 接线（docs 12 章 22.10）：分配失败→GC 重试链 + OOM 预留单例
    dexvm::gc::GcAttach(state);
    dexvm::runtime::EnsureOomReserve(state);

    // DEX 字节需保持存活（DexFile 不持有内存）
    static std::vector<std::unique_ptr<dexvm::session::Session>> sessions;
    static std::vector<std::unique_ptr<dexvm::dex::DexFile>> dex_files;
    sessions.push_back(std::move(session));
    for (const auto& blob : sessions.back()->dex_blobs()) {
      auto dex = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &error);
      if (dex == nullptr) {
        result = "FAIL dex " + blob.name + ": " + error;
        break;
      }
      linker->AddAppDexFile(dex.get());
      dex_files.push_back(std::move(dex));
    }
    if (!result.empty()) break;

    // 定义并初始化全部类（跑 <clinit>）
    int defined = 0, inited = 0;
    for (auto& d : dex_files) {
      for (uint32_t i = 0; i < d->NumClassDefs(); i++) {
        std::string desc(d->GetTypeDescriptor(d->GetClassDef(i).class_idx));
        auto* klass = linker->FindClass(desc);
        if (klass == nullptr) {
          result = "FAIL define " + desc + ": " + linker->last_error();
          break;
        }
        defined++;
        if (linker->InitializeClass(klass, &state)) inited++;
      }
      if (!result.empty()) break;
    }
    if (!result.empty()) break;

    char buf[128];
    std::snprintf(buf, sizeof(buf), "OK classes=%d initialized=%d", defined, inited);
    result = buf;

    // fixture 冒烟：fixture.Calc.add(2,3)（存在则调用）
    auto* calc = linker->FindClass("Lfixture/Calc;");
    if (calc != nullptr) {
      for (auto& m : calc->direct_methods) {
        if (m.name == "add" && m.shorty == "III") {
          dexvm::runtime::Value args[2], ret;
          args[0].i = 2;
          args[1].i = 3;
          if (dexvm::runtime::InvokeMethod(state, &m, args, 2, &ret)) {
            std::snprintf(buf, sizeof(buf), " add(2,3)=%d", ret.i);
            result += buf;
          } else {
            result += " add-failed";
          }
          break;
        }
      }
    }
  } while (false);

  env->functions->ReleaseStringUTFChars(env, dex_path, path);
  return env->functions->NewStringUTF(env, result.c_str());
}

// ---- 生命周期入口 guest 主任务装配（实现于本 TU 后段：依赖 semface/
// ExamDeviceContext 定义）。设备 UI 打开 app 的语义链 = Application 装配 +
// Context 服务单例（PackageManager/ApplicationInfo/Resources）+ LifecycleDriver
// 全程在 main GuestTask 上跑（Thread.start/Handler/Looper 前提，docs 14 章）。----
struct LifecycleGuestRun {
  dexvm::runtime::VMState* state = nullptr;
  dexvm::runtime::ClassLinker* linker = nullptr;
  const dexvm::session::ManifestInfo* manifest = nullptr;
  dexvm::framework::LifecycleConfig* cfg = nullptr;
  dexvm::framework::LifecycleDriver* driver = nullptr;
  std::string target_apk;
  std::string files_dir;
  dexvm::runtime::VObject* app_loader = nullptr;
  bool driver_ok = false;
  // 常驻会话面（打开 app 后保持 VM 存活：消息泵 + 增量出画 + 点击回灌）
  bool keep_alive = false;
  dexvm::bridge::UIBridge* ui = nullptr;
  dexvm::runtime::VObject** activity_slot = nullptr;  // 首帧钩子回填（driver.Run 内）
  int32_t width = 0;
  int32_t height = 0;
};
bool RunApkLifecycleGuest(LifecycleGuestRun* run, std::string* error);
std::string DirName(const std::string& path);  // 定义于本 TU 后段（exam 入口共用）
bool DeviceLibraryLoader(const std::string& name, std::string* error);  // 定义于本 TU 后段

// ---- 常驻 app 会话状态（JNI 拉帧/点击/停止通道；同一时刻至多一个会话）----
struct ApkSessionState {
  std::mutex mu;
  bool active = false;
  bool stop_requested = false;
  int32_t pending_click = -1;
  uint32_t pending_object_click = 0;  // 快照 handle.object_id（0 = 无待处理点击）
  uint32_t pending_long_object = 0;   // 长按回灌目标（0 = 无）
  uint32_t pending_text_object = 0;   // 文本输入回灌目标（0 = 无）
  std::string pending_text;           // 宿主 EditText 键入的 UTF-8 文本
  uint32_t pending_seek_object = 0;   // SeekBar 拖拽回灌目标（0 = 无）
  int32_t pending_seek_progress = 0;
  int32_t pending_seek_phase = 0;     // 0 progress / 1 start / 2 stop
  uint32_t pending_rating_object = 0; // RatingBar 拖星回灌目标（0 = 无）
  int32_t pending_rating_milli = 0;   // rating * 1000（避免浮点线格式）
  uint32_t pending_drawer_object = 0; // DrawerLayout 开合回灌目标（0 = 无）
  int32_t pending_drawer_open = 0;    // 1 开 / 0 合
  uint32_t pending_menu_object = 0;   // Toolbar 菜单项点击回灌目标（0 = 无）
  int32_t pending_menu_index = 0;     // 菜单项序号
  uint32_t pending_fields_object = 0; // DatePicker/TimePicker 字段回灌目标（0 = 无）
  int32_t pending_fields_a = 0;
  int32_t pending_fields_b = 0;
  int32_t pending_fields_c = 0;
  std::deque<std::vector<uint8_t>> frames;  // UIBridge envelope 有序队列（full+patch）
};
ApkSessionState g_session_state;

bool SessionWakePred(void* arg) {
  auto* s = static_cast<ApkSessionState*>(arg);
  std::lock_guard<std::mutex> lk(s->mu);
  return s->stop_requested || s->pending_click >= 0 || s->pending_object_click != 0 ||
         s->pending_long_object != 0 || s->pending_text_object != 0 ||
         s->pending_seek_object != 0 || s->pending_rating_object != 0 ||
         s->pending_drawer_object != 0 || s->pending_menu_object != 0 ||
         s->pending_fields_object != 0;
}

// M4.1/M4.2 设备化入口：引导 VM 跑目标 APK 全生命周期链 + 首帧管线
// （docs 19.6：addView→traversal→submit→present→mark first frame）。
// 引导模式照抄 tests/core/lifecycle_smoke.cc（boot=runtime.dex，app=目标 APK）。
// 返回：成功 "OK <phase 名...> |log=<application.lifecycleLog> |viewtree=<n>"；
// 失败 "FAIL <error>"。
std::string RunApkLifecycleImpl(const char* boot_path, const char* app_path, bool keep_alive) {
  std::string result;

  do {
    if (boot_path == nullptr || app_path == nullptr) {
      result = "FAIL null path argument";
      break;
    }
    ProgressClear();
    ProgressAppend("target: " + std::string(app_path));
#if defined(__ANDROID__)
    // 启动慢方法采样线程（每 200ms 记录当前 guest 方法；仅 profile 开启时输出）。
    static std::atomic<bool> g_profiler_started{false};
    if (!g_profiler_started.exchange(true, std::memory_order_relaxed)) {
      std::thread([] {
        for (;;) {
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          // 锁内拷出方法名：采样线程不得在会话拆栈后解引用 VMethod*。
          std::string method;
          if (!dexvm::runtime::FormatCurrentGuestMethod(&method)) continue;
          __android_log_print(ANDROID_LOG_INFO, "DexVmProfile", "%s", method.c_str());
        }
      }).detach();
    }
#endif
    // 桥轨 mirror 复位：同进程多次打开 app 时，上一会话的 baseline 会让本次
    // 首帧 present 变成 patch（宿主新物化器无基线 -> 拉不到全量树）。
    dexvm::bridge::ResetUiBridgeProtocol();

    // 引导 VM（docs 03 章 §7；lifecycle_smoke 同款）
    dexvm::mem::Heap heap;
    dexvm::runtime::InternTable intern;
    dexvm::runtime::ClassLinker linker(&heap, &intern);
    dexvm::runtime::RegisterBuiltins(&linker);
    dexvm::runtime::RegisterFrameworkBuiltins(&linker);
    dexvm::runtime::VMState state;
    state.heap = &heap;
    state.intern = &intern;
    state.linker = &linker;
    // 自动 GC 接线（docs 12 章 22.10）：分配失败→GC 重试链 + OOM 预留单例
    dexvm::gc::GcAttach(state);
    dexvm::runtime::EnsureOomReserve(state);

    // DEX 字节与 DexFile 生命周期随本调用（结果串已拷出，无需 static 保活）
    std::string error;
    std::vector<std::unique_ptr<dexvm::session::Session>> sessions;
    std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned_dex;

    // boot classpath：runtime.dex
    auto boot = dexvm::session::Session::Open(boot_path, &error);
    if (boot == nullptr) {
      result = "FAIL open runtime dex: " + error;
      break;
    }
    sessions.push_back(std::move(boot));
    for (const auto& blob : sessions.back()->dex_blobs()) {
      auto dex = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &error);
      if (dex == nullptr) {
        result = "FAIL boot dex " + blob.name + ": " + error;
        break;
      }
      linker.AddBootDexFile(dex.get());
      owned_dex.push_back(std::move(dex));
    }
    if (result.rfind("FAIL", 0) == 0) break;
    // 渲染管线必须在 boot dex 装载后登记：lib 侧 widget（如 FrameLayout）此时
    // 才 Findable，onMeasure/onLayout 覆盖才能落到 lib 类上（早登记 = 覆盖缺失）。
    dexvm::framework::RegisterRenderPipeline(&linker);  // M4.2：measure/layout/draw 面
    // lib 类 API 补桩第二遍（首遍在 RegisterFrameworkBuiltins 时 lib 类不可见）。
    dexvm::runtime::ApplyFrameworkApiStubs(&linker);

    // app loader：目标 APK（Session 借字节；DexFile 随调用存活）
    auto app_session = dexvm::session::Session::Open(app_path, &error);
    if (app_session == nullptr) {
      result = "FAIL open apk: " + error;
      break;
    }
    if (!app_session->has_manifest()) {
      result = "FAIL apk has no manifest";
      break;
    }
    sessions.push_back(std::move(app_session));
    for (const auto& blob : sessions.back()->dex_blobs()) {
      auto dex = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &error);
      if (dex == nullptr) {
        result = "FAIL app dex " + blob.name + ": " + error;
        break;
      }
      linker.AddAppDexFile(dex.get());
      owned_dex.push_back(std::move(dex));
    }
    if (result.rfind("FAIL", 0) == 0) break;

    dexvm::runtime::VObject* app_loader = nullptr;
    {
      dexvm::runtime::VClass* cl = linker.FindClass("Ljava/lang/ClassLoader;");
      app_loader = cl != nullptr ? linker.AllocObject(cl) : nullptr;
      if (app_loader == nullptr) {
        result = "FAIL alloc app ClassLoader: " + linker.last_error();
        break;
      }
      linker.loaders().BindObject(app_loader, linker.loaders().default_app());
      // M5 资源面：APK zip 为 app loader 资源源（META-INF/services 等 entry）
      linker.loaders().default_app()->resource_zips.push_back(app_path);
    }

    // Manifest -> 生命周期配置 -> 驱动全链（S5 失败即停）
    dexvm::framework::LifecycleConfig cfg =
        dexvm::framework::ConfigFromManifest(sessions.back()->manifest());
    dexvm::framework::LifecycleDriver driver(state, app_loader);
    AttachProgressObserver(driver);
    // M4.2：首帧管线钩子（onPostResume 后 addView→traversal→submit→present→mark）
    dexvm::bridge::UIBridge ui;
    dexvm::runtime::VObject* guest_activity = nullptr;  // 会话 loop 的内容根来源
    const int32_t guest_w = g_guest_window_width.load(std::memory_order_relaxed);
    const int32_t guest_h = g_guest_window_height.load(std::memory_order_relaxed);
    driver.set_first_frame_hook([&](dexvm::runtime::VObject* activity,
                                    const dexvm::framework::PhaseRunner& run_phase,
                                    std::string* error) {
      guest_activity = activity;
      bool ok = dexvm::framework::RunFirstFramePipeline(state, activity, &ui, guest_w, guest_h,
                                                        run_phase, error);
      // 首帧已出现：恢复网络。启动期的离线快失败只服务于“先出基础界面”；
      // 会话 tick 里 app 的重试/配置请求正常走宿主网络。
      dexvm::runtime::SetBootNetworkOffline(false);
      dexvm::runtime::SetGuestMethodProfile(false);
      return ok;
    });
    // 生命周期全链 + 首帧：main GuestTask 上跑（semface Context 单例装配 +
    // 协程调度器；Thread/Handler/Looper 语义前提）
    {
      // 首帧前离线启动（仅常驻会话）：MainActivity.onCreate 里串行的配置/
      // 埋点请求立即失败，直接走本地默认配置和骨架，避免 30s+ 网络等待；
      // 首帧钩子成功后恢复。
      dexvm::runtime::SetBootNetworkOffline(keep_alive);
      dexvm::runtime::SetGuestMethodProfile(keep_alive);
      LifecycleGuestRun run;
      run.state = &state;
      run.linker = &linker;
      run.manifest = &sessions.back()->manifest();
      run.cfg = &cfg;
      run.driver = &driver;
      run.target_apk = app_path;
      run.files_dir = DirName(app_path);
      run.app_loader = app_loader;
      run.keep_alive = keep_alive;
      run.ui = &ui;
      run.activity_slot = &guest_activity;
      run.width = guest_w;
      run.height = guest_h;
      std::string guest_err;
      if (!RunApkLifecycleGuest(&run, &guest_err)) {
        result = "FAIL guest runtime: " + guest_err;
        break;
      }
      if (!run.driver_ok) {
        result = "FAIL " + driver.error();
        break;
      }
    }

    // 成功：OK + 空格分隔 phase 名 + |log=<application.lifecycleLog>
    std::string out = "OK";
    for (const auto& p : driver.phases()) {
      out += " " + p.name;
    }
    out += " |log=";

    std::string log;
    dexvm::runtime::VClass* app_cls =
        linker.FindClass(dexvm::session::DotToDescriptor(cfg.application_class));
    if (app_cls != nullptr) {
      for (dexvm::runtime::VClass* k = app_cls; k != nullptr && log.empty(); k = k->super_class) {
        for (const auto& f : k->static_fields) {
          if (f.name != "lifecycleLog" || !f.is_static) continue;
          if (f.offset < app_cls->static_storage.size()) {
            const dexvm::runtime::Value& slot = app_cls->static_storage[f.offset];
            if (slot.ref != nullptr) {
              log = static_cast<dexvm::runtime::VString*>(slot.ref)->ToModifiedUtf8();
            }
          }
          break;
        }
      }
    }
    out += log;
    // 桥轨首帧标注（缝B PresentViewTree 产物；自绘位图链已退役，位图经
    // nativeGetViewSnapshot envelope 宿主物化出画）
    {
      char frame_buf[64];
      std::snprintf(frame_buf, sizeof(frame_buf), " |viewtree=%d", ui.view_tree_present_count());
      out += frame_buf;
    }
    result = out;
  } while (false);

  // 异常/提前退出兜底：不让离线/采样开关泄漏到后续会话或宿主线程。
  dexvm::runtime::SetBootNetworkOffline(false);
  dexvm::runtime::SetGuestMethodProfile(false);
  return result;
}

jstring NativeRunApkLifecycle(JNIEnv* env, jclass, jstring runtime_dex_path, jstring apk_path) {
  const char* boot_path = env->functions->GetStringUTFChars(env, runtime_dex_path, nullptr);
  const char* app_path = env->functions->GetStringUTFChars(env, apk_path, nullptr);
  std::string result = RunApkLifecycleImpl(boot_path, app_path, false);
  if (boot_path != nullptr) env->functions->ReleaseStringUTFChars(env, runtime_dex_path, boot_path);
  if (app_path != nullptr) env->functions->ReleaseStringUTFChars(env, apk_path, app_path);
  return env->functions->NewStringUTF(env, result.c_str());
}

// 常驻会话入口：首帧后保持 VM 存活（消息泵/增量出画/点击回灌），宿主
// nativeSessionStop 后返回最终 phase 报告（尾部 report 与 one-shot 同形）。
jstring NativeRunApkSession(JNIEnv* env, jclass, jstring runtime_dex_path, jstring apk_path) {
  // 宿主 JavaVM 交接（native -> 宿主 Java 字体桥；core/host_java.h）。
  JavaVM* host_vm = nullptr;
  env->functions->GetJavaVM(env, &host_vm);
  dexvm::runtime::SetHostJavaVM(reinterpret_cast<void*>(host_vm));
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DexVmFont", "NativeRunApkSession host vm=%p",
                      reinterpret_cast<void*>(host_vm));
#endif
  const char* boot_path = env->functions->GetStringUTFChars(env, runtime_dex_path, nullptr);
  const char* app_path = env->functions->GetStringUTFChars(env, apk_path, nullptr);
  std::string result = RunApkLifecycleImpl(boot_path, app_path, true);
  if (boot_path != nullptr) env->functions->ReleaseStringUTFChars(env, runtime_dex_path, boot_path);
  if (app_path != nullptr) env->functions->ReleaseStringUTFChars(env, apk_path, app_path);
  return env->functions->NewStringUTF(env, result.c_str());
}

// ---- 常驻会话 JNI 通道（宿主轮询拉帧/点击/停止）----
jbyteArray NativeSessionPollFrames(JNIEnv* env, jclass) {
  std::deque<std::vector<uint8_t>> frames;
  {
    std::lock_guard<std::mutex> lk(g_session_state.mu);
    if (g_session_state.frames.empty()) return nullptr;
    frames.swap(g_session_state.frames);
  }
  size_t total = 4;
  for (const auto& f : frames) total += 4 + f.size();
  jbyteArray arr = env->functions->NewByteArray(env, static_cast<jsize>(total));
  if (arr == nullptr) return nullptr;
  std::vector<uint8_t> blob(total);
  size_t off = 0;
  auto put_u32 = [&](uint32_t v) {
    blob[off++] = static_cast<uint8_t>(v);
    blob[off++] = static_cast<uint8_t>(v >> 8);
    blob[off++] = static_cast<uint8_t>(v >> 16);
    blob[off++] = static_cast<uint8_t>(v >> 24);
  };
  put_u32(static_cast<uint32_t>(frames.size()));
  for (const auto& f : frames) {
    put_u32(static_cast<uint32_t>(f.size()));
    std::memcpy(blob.data() + off, f.data(), f.size());
    off += f.size();
  }
  env->functions->SetByteArrayRegion(env, arr, 0, static_cast<jsize>(blob.size()),
                                     reinterpret_cast<const jbyte*>(blob.data()));
  return arr;
}

// ---- 宿主字体 worker 通道（com.dexvm.host.FontBridge；Java->native 方向）----
jlong NativeFontNextRequest(JNIEnv*, jclass) {
  return static_cast<jlong>(dexvm::runtime::HostFontNextRequest());
}

jint NativeFontOp(JNIEnv*, jclass, jlong id) {
  return dexvm::runtime::HostFontOp(static_cast<uint64_t>(id));
}

jint NativeFontStyle(JNIEnv*, jclass, jlong id) {
  return dexvm::runtime::HostFontStyle(static_cast<uint64_t>(id));
}

jfloat NativeFontSize(JNIEnv*, jclass, jlong id) {
  return dexvm::runtime::HostFontSize(static_cast<uint64_t>(id));
}

jlong NativeFontBase(JNIEnv*, jclass, jlong id) {
  return static_cast<jlong>(dexvm::runtime::HostFontBase(static_cast<uint64_t>(id)));
}

jstring NativeFontString(JNIEnv* env, jclass, jlong id) {
  std::string s = dexvm::runtime::HostFontString(static_cast<uint64_t>(id));
  return env->functions->NewStringUTF(env, s.c_str());
}

jstring NativeFontText(JNIEnv* env, jclass, jlong id) {
  std::u16string s = dexvm::runtime::HostFontText(static_cast<uint64_t>(id));
  return env->functions->NewString(env, reinterpret_cast<const jchar*>(s.data()),
                                   static_cast<jsize>(s.size()));
}

void NativeFontComplete(JNIEnv* env, jclass, jlong id, jlong handle, jfloat width,
                        jintArray metrics, jboolean metrics_ok) {
  int32_t out[5] = {0, 0, 0, 0, 0};
  bool ok = metrics_ok == JNI_TRUE && metrics != nullptr;
  if (ok && env->functions->GetArrayLength(env, metrics) >= 5) {
    jint vals[5] = {0, 0, 0, 0, 0};
    env->functions->GetIntArrayRegion(env, metrics, 0, 5, vals);
    for (int i = 0; i < 5; i++) out[i] = vals[i];
  } else {
    ok = false;
  }
  dexvm::runtime::HostFontComplete(static_cast<uint64_t>(id), handle, width, out, ok);
}

// ---- AOSP starting window（首帧前预览层）----
// VM 侧按 activity 主题解析 windowBackground（framework ResolveStartingWindowTheme），
// 宿主仅按协议显示：u32 bgColor / u32 gravity / u32 bitmapLen / bitmap bytes。
namespace {
struct StartingWindowInfo {
  bool valid = false;
  uint32_t color = 0xFFFFFFFFu;
  int32_t gravity = 0x11;
  std::vector<uint8_t> bitmap;
};
std::mutex g_starting_window_mu;
StartingWindowInfo g_starting_window;
}  // namespace

jbyteArray NativeGetStartingWindow(JNIEnv* env, jclass) {
  StartingWindowInfo info;
  {
    std::lock_guard<std::mutex> lk(g_starting_window_mu);
    if (!g_starting_window.valid) return nullptr;
    info = g_starting_window;
  }
  size_t total = 12 + info.bitmap.size();
  jbyteArray arr = env->functions->NewByteArray(env, static_cast<jsize>(total));
  if (arr == nullptr) return nullptr;
  std::vector<uint8_t> blob(total);
  auto put_u32 = [&](size_t off, uint32_t v) {
    blob[off] = static_cast<uint8_t>(v);
    blob[off + 1] = static_cast<uint8_t>(v >> 8);
    blob[off + 2] = static_cast<uint8_t>(v >> 16);
    blob[off + 3] = static_cast<uint8_t>(v >> 24);
  };
  put_u32(0, info.color);
  put_u32(4, static_cast<uint32_t>(info.gravity));
  put_u32(8, static_cast<uint32_t>(info.bitmap.size()));
  if (!info.bitmap.empty()) {
    std::memcpy(blob.data() + 12, info.bitmap.data(), info.bitmap.size());
  }
  env->functions->SetByteArrayRegion(env, arr, 0, static_cast<jsize>(blob.size()),
                                     reinterpret_cast<const jbyte*>(blob.data()));
  return arr;
}

void NativeSessionStop(JNIEnv*, jclass) {
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  g_session_state.stop_requested = true;
}

jboolean NativeSessionIsRunning(JNIEnv*, jclass) {
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  return g_session_state.active ? JNI_TRUE : JNI_FALSE;
}

// 会话窗口逻辑尺寸（宿主按内容区宽高比设定；首帧前/会话启动前调用）。
// 不改变 guest 密度（2.0），只调整窗口高度，使宿主物化画面等比缩放。
void NativeSetGuestWindowSize(JNIEnv*, jclass, jint width, jint height) {
  int32_t w = width < 320 ? 320 : (width > 2048 ? 2048 : width);
  int32_t h = height < 480 ? 480 : (height > 4096 ? 4096 : height);
  g_guest_window_width.store(w, std::memory_order_relaxed);
  g_guest_window_height.store(h, std::memory_order_relaxed);
}

// 调试面：guest 方法入口 trace（逗号分隔类 descriptor 子串；空 = 关闭）。
void NativeSetGuestTrace(JNIEnv* env, jclass, jstring patterns) {
  std::string csv;
  if (patterns != nullptr) {
    const char* p = env->functions->GetStringUTFChars(env, patterns, nullptr);
    if (p != nullptr) {
      csv = p;
      env->functions->ReleaseStringUTFChars(env, patterns, p);
    }
  }
  dexvm::runtime::SetGuestMethodTrace(std::move(csv));
}

std::string DescribeGuestException(dexvm::runtime::ObjHeader* ex);

// 设备 TLS 桥接自检：引导 runtime.dex 后调用 guest TlsSelfTest.run(host,port)。
// 覆盖 OkHttp 同款链路（TrustManagerFactory -> SSLContext -> createSocket(wrap)
// -> 宿主 TlsBridge.java -> 真实 TLS）。返回 "OK ..."/"FAIL ..." 诊断串。
jstring NativeRunTlsSelfTest(JNIEnv* env, jclass, jstring runtime_dex_path, jstring host_str) {
  using dexvm::runtime::Value;
  using dexvm::runtime::VClass;
  using dexvm::runtime::VMethod;
  using dexvm::runtime::VString;
  using dexvm::runtime::VMState;
  const char* dex_path =
      runtime_dex_path != nullptr
          ? env->functions->GetStringUTFChars(env, runtime_dex_path, nullptr)
          : nullptr;
  const char* host =
      host_str != nullptr ? env->functions->GetStringUTFChars(env, host_str, nullptr) : nullptr;
  std::string result;
  do {
    if (dex_path == nullptr || host == nullptr) {
      result = "FAIL null argument";
      break;
    }
    dexvm::mem::Heap heap;
    dexvm::runtime::InternTable intern;
    dexvm::runtime::ClassLinker linker(&heap, &intern);
    dexvm::runtime::RegisterBuiltins(&linker);
    dexvm::runtime::RegisterFrameworkBuiltins(&linker);
    std::string err;
    auto session = dexvm::session::Session::Open(dex_path, &err);
    if (session == nullptr) {
      result = "FAIL open runtime.dex: " + err;
      break;
    }
    std::vector<std::unique_ptr<dexvm::dex::DexFile>> dex_files;
    bool dex_ok = true;
    for (const auto& blob : session->dex_blobs()) {
      auto dex = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &err);
      if (dex == nullptr) {
        result = "FAIL dex " + blob.name + ": " + err;
        dex_ok = false;
        break;
      }
      linker.AddBootDexFile(dex.get());
      dex_files.push_back(std::move(dex));
    }
    if (!dex_ok) break;
    VMState state;
    state.heap = &heap;
    state.intern = &intern;
    state.linker = &linker;
    dexvm::gc::GcAttach(state);
    dexvm::runtime::EnsureOomReserve(state);
    dexvm::runtime::SetGuestSchedOps(dexvm::runtime::VmSchedBackendOps());
    dexvm::runtime::SetGuestRunBudgetMs(120000);
    std::string guest_result;
    bool invoked = dexvm::runtime::RunGuestMain(state, [&](VMState& s) -> bool {
      VClass* k = linker.FindClass("Lcom/dexvm/runtime/TlsSelfTest;");
      if (k == nullptr) {
        guest_result = "FAIL TlsSelfTest class not found";
        return true;
      }
      if (!linker.InitializeClass(k, &s)) {
        guest_result = "FAIL TlsSelfTest clinit: " + DescribeGuestException(s.pending_exception);
        s.ClearException();
        return true;
      }
      VMethod* run = nullptr;
      for (auto* list : {&k->direct_methods, &k->virtual_methods}) {
        for (auto& m : *list) {
          if (m.name == "run" && m.IsStatic() && m.param_types.size() == 2 &&
              m.param_types[0] == "Ljava/lang/String;" && m.param_types[1] == "I" &&
              m.return_type == "Ljava/lang/String;") {
            run = &m;
            break;
          }
        }
        if (run != nullptr) break;
      }
      if (run == nullptr) {
        guest_result = "FAIL TlsSelfTest.run(String,int) not found";
        return true;
      }
      Value args[2] = {};
      args[0].ref = reinterpret_cast<dexvm::runtime::ObjHeader*>(
          linker.AllocString(std::u16string(host, host + std::strlen(host))));
      args[1].i = 443;
      Value out{};
      if (!InvokeMethod(s, run, args, 2, &out) || out.ref == nullptr) {
        guest_result = "FAIL TlsSelfTest.run threw: " + DescribeGuestException(s.pending_exception);
        s.ClearException();
        return true;
      }
      guest_result = static_cast<VString*>(out.ref)->ToModifiedUtf8();
      return true;
    });
    if (!invoked && guest_result.empty()) {
      guest_result = std::string("FAIL guest run: ") +
                     dexvm::runtime::GuestRunFailureReason();
    }
    result = guest_result.empty() ? "FAIL empty guest result" : guest_result;
  } while (false);
  if (dex_path != nullptr) {
    env->functions->ReleaseStringUTFChars(env, runtime_dex_path, dex_path);
  }
  if (host != nullptr) env->functions->ReleaseStringUTFChars(env, host_str, host);
  if (env->functions->ExceptionCheck(env)) env->functions->ExceptionClear(env);
  return env->functions->NewStringUTF(env, result.c_str());
}

// ---- guest TLS 桥接通道（TlsBridge.java 专用线程消费；core/tls_bridge.h）----
// nativeTlsTake 阻塞在请求队列上（宿主 Java 线程，不是 guest 线程）；guest 侧
// 的等待在 core/tls_bridge.cc 内经 GuestSched 协作切片 + 跨线程 wake 完成。
jbyteArray NativeTlsTake(JNIEnv* env, jclass) {
  // 宿主桥接线程已就绪：guest BridgedTls.open 的 availability 判据。
  dexvm::runtime::TlsSetBridgeAvailable(true);
  dexvm::runtime::TlsRequest request;
  if (!dexvm::runtime::TlsTakeRequest(&request)) return nullptr;  // shutdown
  std::string op = dexvm::runtime::TlsEncodeRequest(request);
  jbyteArray arr = env->functions->NewByteArray(env, static_cast<jsize>(op.size()));
  if (arr == nullptr) return nullptr;
  env->functions->SetByteArrayRegion(env, arr, 0, static_cast<jsize>(op.size()),
                                     reinterpret_cast<const jbyte*>(op.data()));
  return arr;
}

void NativeTlsReply(JNIEnv* env, jclass, jlong id, jint status, jlong handle,
                    jbyteArray data) {
  std::string bytes;
  if (data != nullptr) {
    const jsize len = env->functions->GetArrayLength(env, data);
    if (len > 0) {
      bytes.resize(static_cast<size_t>(len));
      env->functions->GetByteArrayRegion(env, data, 0, len,
                                         reinterpret_cast<jbyte*>(bytes.data()));
    }
  }
  dexvm::runtime::TlsDeliverReply(static_cast<int64_t>(id), static_cast<int32_t>(status),
                                  static_cast<int64_t>(handle), bytes);
}

// =====================================================================
// M3 加固考卷设备化（nativeRunExam）：
// exam_smoke 同款引导 + LifecycleDriver 驱动 + 逐 phase/缺口诊断；
// System.loadLibrary 设备钩子接真实 loader（lib/<abi>/*.so 解压 ->
// dlopen -> JNI_OnLoad(VM JNIEnvExt) -> RegisterNatives 生效）。
// 报告：phase 序列、停点、缺口清单（分类+guest 栈）、native 库执行结果。
// =====================================================================

// ---- 最小 zip 索引 + 单 entry 解压（APK lib/<abi>/*.so 提取；
//      格式同 session.cc：EOCD -> central directory -> local entry）----
constexpr uint32_t kZipSigLocal = 0x04034b50;
constexpr uint32_t kZipSigCentral = 0x02014b50;
constexpr uint32_t kZipSigEocd = 0x06054b50;
constexpr uint16_t kMethodStored = 0;
constexpr uint16_t kMethodDeflate = 8;

#if defined(__aarch64__)
constexpr const char* kAbiDir = "lib/arm64-v8a/";
#elif defined(__x86_64__)
constexpr const char* kAbiDir = "lib/x86_64/";
#elif defined(__arm__)
constexpr const char* kAbiDir = "lib/armeabi-v7a/";
#else
constexpr const char* kAbiDir = "lib/x86/";
#endif

uint16_t Rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool InflateRaw(const uint8_t* src, size_t src_len, size_t dst_len, std::vector<uint8_t>* out) {
  out->resize(dst_len);
  z_stream zs{};
  zs.next_in = const_cast<Bytef*>(src);
  zs.avail_in = static_cast<uInt>(src_len);
  zs.next_out = out->data();
  zs.avail_out = static_cast<uInt>(dst_len);
  if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;  // raw deflate
  int rc = inflate(&zs, Z_FINISH);
  inflateEnd(&zs);
  return rc == Z_STREAM_END && zs.total_out == dst_len;
}

bool FileExists(const std::string& path) {
  struct stat st{};
  return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// 考卷阶段面包屑（logcat tag=DexVmTargetExam；hang 定位用）
void ExamBreadcrumb(const char* fmt, ...) {
#if defined(__ANDROID__)
  va_list ap;
  va_start(ap, fmt);
  __android_log_vprint(ANDROID_LOG_INFO, "DexVmTargetExam", fmt, ap);
  va_end(ap);
#else
  (void)fmt;
#endif
}

std::string DirName(const std::string& path) {
  size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0) return "/";
  return path.substr(0, slash);
}

std::string BaseName(const std::string& path) {
  size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

struct ApkSoEntry {
  std::string name;  // zip 内全名 "lib/<abi>/libX.so"
  uint16_t method = 0;
  uint32_t comp_size = 0;
  uint32_t uncomp_size = 0;
  uint32_t local_off = 0;
};

class ApkSoIndex {
 public:
  // 解析 central directory，收集 lib/<abi>/*.so（目标 ABI 与 VM 同架构）
  bool Build(const std::string& apk_path, std::string* error) {
    apk_path_ = apk_path;
    entries_.clear();
    abi_total_ = 0;
    other_abi_ = 0;
    FILE* f = std::fopen(apk_path.c_str(), "rb");
    if (f == nullptr) {
      *error = "cannot open apk: " + apk_path;
      return false;
    }
    auto fail = [&](const std::string& e) {
      std::fclose(f);
      *error = e;
      return false;
    };
    std::fseek(f, 0, SEEK_END);
    long fsize = std::ftell(f);
    if (fsize < 22) return fail("apk too small");
    size_t tail = fsize >= 65557 ? 65557 : static_cast<size_t>(fsize);
    std::fseek(f, fsize - static_cast<long>(tail), SEEK_SET);
    std::vector<uint8_t> buf(tail);
    if (std::fread(buf.data(), 1, tail, f) != tail) return fail("apk tail read failed");
    // EOCD（注释最长 64K）：自尾部向前扫描
    size_t eocd = std::string::npos;
    for (size_t i = tail - 22 + 1; i-- > 0;) {
      if (Rd32(buf.data() + i) == kZipSigEocd) {
        eocd = i;
        break;
      }
    }
    if (eocd == std::string::npos) return fail("zip EOCD not found");
    uint16_t count = Rd16(buf.data() + eocd + 10);
    uint32_t cd_size = Rd32(buf.data() + eocd + 12);
    uint32_t cd_off = Rd32(buf.data() + eocd + 16);
    if (cd_size > 256u * 1024u * 1024u || static_cast<long>(cd_off) + cd_size > fsize) {
      return fail("bad central directory bounds");
    }
    std::vector<uint8_t> cd(cd_size);
    std::fseek(f, static_cast<long>(cd_off), SEEK_SET);
    if (cd_size > 0 && std::fread(cd.data(), 1, cd_size, f) != cd_size) {
      return fail("central directory read failed");
    }
    std::fclose(f);
    size_t p = 0;
    for (uint16_t i = 0; i < count && p + 46 <= cd.size(); i++) {
      if (Rd32(cd.data() + p) != kZipSigCentral) return fail("bad central directory entry");
      uint16_t name_len = Rd16(cd.data() + p + 28);
      uint16_t extra_len = Rd16(cd.data() + p + 30);
      uint16_t comment_len = Rd16(cd.data() + p + 32);
      if (p + 46 + name_len > cd.size()) return fail("central name out of bounds");
      std::string name(reinterpret_cast<const char*>(cd.data() + p + 46), name_len);
      bool is_so = name.rfind("lib/", 0) == 0 && name.size() > 4 &&
                   name.compare(name.size() - 3, 3, ".so") == 0;
      if (is_so && name.rfind(kAbiDir, 0) == 0) {
        ApkSoEntry e;
        e.name = name;
        e.method = Rd16(cd.data() + p + 10);
        e.comp_size = Rd32(cd.data() + p + 20);
        e.uncomp_size = Rd32(cd.data() + p + 24);
        e.local_off = Rd32(cd.data() + p + 42);
        entries_.push_back(std::move(e));
        abi_total_ += entries_.back().uncomp_size;
      } else if (is_so) {
        other_abi_++;
      }
      p += 46u + name_len + extra_len + comment_len;
    }
    return true;
  }

  const ApkSoEntry* Find(const std::string& base_name) const {
    for (const auto& e : entries_) {
      if (BaseName(e.name) == base_name) return &e;
    }
    return nullptr;
  }

  // 解出单 entry 到 dest（stored 直拷 / deflate 经 zlib raw）
  bool ExtractTo(const ApkSoEntry& e, const std::string& dest, std::string* error) const {
    FILE* f = std::fopen(apk_path_.c_str(), "rb");
    if (f == nullptr) {
      *error = "cannot reopen apk";
      return false;
    }
    auto fail = [&](const std::string& msg) {
      std::fclose(f);
      *error = msg;
      return false;
    };
    uint8_t lh[30];
    std::fseek(f, static_cast<long>(e.local_off), SEEK_SET);
    if (std::fread(lh, 1, 30, f) != 30) return fail("local header read failed");
    if (Rd32(lh) != kZipSigLocal) return fail("bad local header sig");
    uint32_t data_off = e.local_off + 30u + Rd16(lh + 26) + Rd16(lh + 28);
    if (e.comp_size > 512u * 1024u * 1024u) return fail("entry too large");
    std::vector<uint8_t> comp(e.comp_size);
    std::fseek(f, static_cast<long>(data_off), SEEK_SET);
    if (e.comp_size > 0 && std::fread(comp.data(), 1, e.comp_size, f) != e.comp_size) {
      return fail("entry data read failed");
    }
    std::fclose(f);
    std::vector<uint8_t> out;
    if (e.method == kMethodStored) {
      out = std::move(comp);
    } else if (e.method == kMethodDeflate) {
      if (!InflateRaw(comp.data(), comp.size(), e.uncomp_size, &out)) return fail("inflate failed");
    } else {
      return fail("unsupported zip method " + std::to_string(e.method));
    }
    if (out.size() != e.uncomp_size) return fail("size mismatch after decompress");
    FILE* o = std::fopen(dest.c_str(), "wb");
    if (o == nullptr) return fail("cannot write " + dest);
    if (std::fwrite(out.data(), 1, out.size(), o) != out.size()) {
      std::fclose(o);
      return fail("short write " + dest);
    }
    std::fclose(o);
    return true;
  }

  const std::vector<ApkSoEntry>& entries() const { return entries_; }
  uint64_t abi_total_bytes() const { return abi_total_; }
  int other_abi_count() const { return other_abi_; }

 private:
  std::string apk_path_;
  std::vector<ApkSoEntry> entries_;
  uint64_t abi_total_ = 0;
  int other_abi_ = 0;
};

// ---- 设备 System.loadLibrary 真 loader（builtins.h LibraryLoadHook 落点）----
// 名 -> nativeLibDir/<name>（缺则从 APK lib/<abi>/ 提取）-> NativeManager
// LoadNativeLibrary（dlopen -> JNI_OnLoad(VM JNIEnvExt) -> RegisterNatives 生效）。
struct LibLoadRecord {
  std::string name;    // 请求名（libX.so 或路径）
  std::string path;    // 解析到的 .so 路径
  std::string extract; // 提取说明
  bool ok = false;
  std::string detail;  // dlopen / JNI_OnLoad 结果或错误
};

struct ExamDeviceContext {
  dexvm::jni::JavaVMExt* vm = nullptr;
  dexvm::jni::JNIEnvExt* env = nullptr;
  dexvm::runtime::VMState* state = nullptr;
  dexvm::runtime::VObject* app_loader = nullptr;
  ApkSoIndex so_index;
  std::string native_lib_dir;
  std::vector<LibLoadRecord> log;
  // JNI 面包屑（诊断）：FindClass/Get*ID/字段/字符串面调用轨迹（尾部 40 条入报告）
  std::vector<std::string> jni_trace;
  size_t jni_trace_calls = 0;  // 总笔数（环形队列丢尾前的全部调用计数）
  // VM 语义面补全单例（ContextImpl/PackageManager/PackageInfo/ApplicationInfo）
  dexvm::runtime::VObject* system_context = nullptr;
  dexvm::runtime::VObject* package_manager = nullptr;
  dexvm::runtime::VObject* package_info = nullptr;
  dexvm::runtime::VObject* application_info = nullptr;
  dexvm::runtime::VObject* package_name_str = nullptr;
  dexvm::runtime::VObject* external_storage = nullptr;  // java.io.File("/storage/emulated/0")
};

// NativeRunExam 生命周期内有效（M3 单线程；结束后解除）
ExamDeviceContext* g_exam = nullptr;

void TraceAdd(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void TraceAdd(const char* fmt, ...) {
  if (g_exam == nullptr) return;
  g_exam->jni_trace_calls++;  // 总笔数（环形队列只留尾部；区分"循环仍在跑"与陈旧尾迹）
  if (g_exam->jni_trace.size() >= 512) {
    g_exam->jni_trace.erase(g_exam->jni_trace.begin(), g_exam->jni_trace.begin() + 128);
  }
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) g_exam->jni_trace.push_back(std::string(buf, static_cast<size_t>(n)));
}

// ---- JNI 诊断面包屑：包装 guest JNIEnv 函数表（转发原实现 + 记录调用/结果）----
// 目的：JNI_OnLoad 真实执行的缺口归因（哪次 FindClass/GetFieldID/GetStringUTFChars
// 返回 NULL 导致 guest native 崩溃——诊断面，不改 jni_env 语义）。
const JNINativeInterface* g_orig_table = nullptr;
JNINativeInterface g_trace_table;

jclass JNICALL T_FindClass(JNIEnv* env, const char* name) {
  jclass r = g_orig_table->FindClass(env, name);
  TraceAdd("FindClass(%s) -> %p", name != nullptr ? name : "(null)", (void*)r);
  return r;
}
jmethodID JNICALL T_GetMethodID(JNIEnv* env, jclass c, const char* name, const char* sig) {
  jmethodID r = g_orig_table->GetMethodID(env, c, name, sig);
  TraceAdd("GetMethodID(%p, %s, %s) -> %p", (void*)c, name != nullptr ? name : "(null)",
           sig != nullptr ? sig : "(null)", (void*)r);
  return r;
}
jmethodID JNICALL T_GetStaticMethodID(JNIEnv* env, jclass c, const char* name, const char* sig) {
  jmethodID r = g_orig_table->GetStaticMethodID(env, c, name, sig);
  TraceAdd("GetStaticMethodID(%p, %s, %s) -> %p", (void*)c, name != nullptr ? name : "(null)",
           sig != nullptr ? sig : "(null)", (void*)r);
  return r;
}
jfieldID JNICALL T_GetFieldID(JNIEnv* env, jclass c, const char* name, const char* sig) {
  jfieldID r = g_orig_table->GetFieldID(env, c, name, sig);
  TraceAdd("GetFieldID(%p, %s, %s) -> %p", (void*)c, name != nullptr ? name : "(null)",
           sig != nullptr ? sig : "(null)", (void*)r);
  return r;
}
jfieldID JNICALL T_GetStaticFieldID(JNIEnv* env, jclass c, const char* name, const char* sig) {
  jfieldID r = g_orig_table->GetStaticFieldID(env, c, name, sig);
  TraceAdd("GetStaticFieldID(%p, %s, %s) -> %p", (void*)c, name != nullptr ? name : "(null)",
           sig != nullptr ? sig : "(null)", (void*)r);
  return r;
}
jobject JNICALL T_GetObjectField(JNIEnv* env, jobject o, jfieldID f) {
  jobject r = g_orig_table->GetObjectField(env, o, f);
  TraceAdd("GetObjectField(%p, fid=%p) -> %p", (void*)o, (void*)f, (void*)r);
  return r;
}
jobject JNICALL T_GetStaticObjectField(JNIEnv* env, jclass c, jfieldID f) {
  jobject r = g_orig_table->GetStaticObjectField(env, c, f);
  TraceAdd("GetStaticObjectField(%p, fid=%p) -> %p", (void*)c, (void*)f, (void*)r);
  return r;
}
const char* JNICALL T_GetStringUTFChars(JNIEnv* env, jstring s, jboolean* is_copy) {
  const char* r = g_orig_table->GetStringUTFChars(env, s, is_copy);
  std::string preview;
  if (r != nullptr) {
    preview = std::string(r, strnlen(r, 120));
  } else if (s != nullptr) {
    preview = static_cast<dexvm::runtime::VString*>(
                  reinterpret_cast<dexvm::runtime::ObjHeader*>(s))
                  ->ToModifiedUtf8();
    if (preview.size() > 120) preview = preview.substr(0, 120);
    preview = "\"" + preview + "\" -> NULL";
  } else {
    preview = "(str=null)";
  }
  TraceAdd("GetStringUTFChars(%p) -> %p %s", (void*)s, (void*)r,
           r != nullptr ? ("\"" + preview + "\"").c_str() : preview.c_str());
  return r;
}
jobject JNICALL T_CallObjectMethod(JNIEnv* env, jobject o, jmethodID m, ...) {
  va_list ap;
  va_start(ap, m);
  jobject r = g_orig_table->CallObjectMethodV(env, o, m, ap);
  va_end(ap);
  TraceAdd("CallObjectMethod(%p, mid=%p) -> %p", (void*)o, (void*)m, (void*)r);
  return r;
}
jobject JNICALL T_CallObjectMethodV(JNIEnv* env, jobject o, jmethodID m, va_list ap) {
  jobject r = g_orig_table->CallObjectMethodV(env, o, m, ap);
  TraceAdd("CallObjectMethodV(%p, mid=%p) -> %p", (void*)o, (void*)m, (void*)r);
  return r;
}
jobject JNICALL T_CallStaticObjectMethod(JNIEnv* env, jclass c, jmethodID m, ...) {
  va_list ap;
  va_start(ap, m);
  jobject r = g_orig_table->CallStaticObjectMethodV(env, c, m, ap);
  va_end(ap);
  TraceAdd("CallStaticObjectMethod(%p, mid=%p) -> %p", (void*)c, (void*)m, (void*)r);
  return r;
}
jobject JNICALL T_CallStaticObjectMethodV(JNIEnv* env, jclass c, jmethodID m, va_list ap) {
  jobject r = g_orig_table->CallStaticObjectMethodV(env, c, m, ap);
  TraceAdd("CallStaticObjectMethodV(%p, mid=%p) -> %p", (void*)c, (void*)m, (void*)r);
  return r;
}
void JNICALL T_CallStaticVoidMethod(JNIEnv* env, jclass c, jmethodID m, ...) {
  va_list ap;
  va_start(ap, m);
  g_orig_table->CallStaticVoidMethodV(env, c, m, ap);
  va_end(ap);
  TraceAdd("CallStaticVoidMethod(%p, mid=%p)", (void*)c, (void*)m);
}
void JNICALL T_CallStaticVoidMethodV(JNIEnv* env, jclass c, jmethodID m, va_list ap) {
  g_orig_table->CallStaticVoidMethodV(env, c, m, ap);
  TraceAdd("CallStaticVoidMethodV(%p, mid=%p)", (void*)c, (void*)m);
}
void JNICALL T_CallVoidMethod(JNIEnv* env, jobject o, jmethodID m, ...) {
  va_list ap;
  va_start(ap, m);
  g_orig_table->CallVoidMethodV(env, o, m, ap);
  va_end(ap);
  TraceAdd("CallVoidMethod(%p, mid=%p)", (void*)o, (void*)m);
}
void JNICALL T_CallVoidMethodV(JNIEnv* env, jobject o, jmethodID m, va_list ap) {
  g_orig_table->CallVoidMethodV(env, o, m, ap);
  TraceAdd("CallVoidMethodV(%p, mid=%p)", (void*)o, (void*)m);
}

// 挂接诊断面（InitJNIEnvExt 之后调用一次；拷表换槽，原表转发）
void InstallJniTrace(dexvm::jni::JNIEnvExt* env) {
  if (env == nullptr || env->functions == nullptr || g_orig_table != nullptr) return;
  g_orig_table = env->functions;
  g_trace_table = *g_orig_table;
  g_trace_table.FindClass = T_FindClass;
  g_trace_table.GetMethodID = T_GetMethodID;
  g_trace_table.GetStaticMethodID = T_GetStaticMethodID;
  g_trace_table.GetFieldID = T_GetFieldID;
  g_trace_table.GetStaticFieldID = T_GetStaticFieldID;
  g_trace_table.GetObjectField = T_GetObjectField;
  g_trace_table.GetStaticObjectField = T_GetStaticObjectField;
  g_trace_table.GetStringUTFChars = T_GetStringUTFChars;
  g_trace_table.CallObjectMethod = T_CallObjectMethod;
  g_trace_table.CallObjectMethodV = T_CallObjectMethodV;
  g_trace_table.CallStaticObjectMethod = T_CallStaticObjectMethod;
  g_trace_table.CallStaticObjectMethodV = T_CallStaticObjectMethodV;
  g_trace_table.CallStaticVoidMethod = T_CallStaticVoidMethod;
  g_trace_table.CallStaticVoidMethodV = T_CallStaticVoidMethodV;
  g_trace_table.CallVoidMethod = T_CallVoidMethod;
  g_trace_table.CallVoidMethodV = T_CallVoidMethodV;
  env->functions = &g_trace_table;
}

// ---- guest native 崩溃护栏（JNI_OnLoad 真实执行的 SIGSEGV 等记录为缺口，不丢报告）----
// 只包住 loader 调用（guest native 是真实执行、可崩的）；VM 自身崩溃不吞（诚实）。
// 诊断位型（可归因）：siginfo_t 的 si_code/si_addr + ucontext PC/SP 记入报告
// （"unknown signal" -> "SEGV_ACCERR @ 0x…" 形态）。jmp_buf 栈化：嵌套
// CrashGuard（loadLibrary 钩子在 guest 执行护栏内）不再互相覆写 jmp_buf/清 armed。
sigjmp_buf g_crash_guard_stack[4];
volatile sig_atomic_t g_crash_depth = 0;  // 嵌套深度（栈顶 = depth-1）
volatile sig_atomic_t g_crash_guard_armed = 0;
volatile sig_atomic_t g_crash_sig = 0;
volatile sig_atomic_t g_crash_si_code = 0;
volatile unsigned long long g_crash_fault_addr = 0;
volatile unsigned long long g_crash_pc = 0;
volatile unsigned long long g_crash_sp = 0;
volatile unsigned long long g_crash_fp = 0;
// 崩溃点栈顶摘要（64 字 @ SP）+ fp-0x10 两槽（ScanRangeWords 形参溢出槽 =
// 被扫区间 base/bytes——区间归因位型）。信号处理器内只做定长 memcpy 读。
volatile unsigned long long g_crash_stack[8] = {};
volatile unsigned long long g_crash_fparea[2] = {};

extern "C" void ExamCrashHandler(int sig, siginfo_t* si, void* uc) {
  if (!g_crash_guard_armed || g_crash_depth <= 0) {
    ::signal(sig, SIG_DFL);
    ::raise(sig);
    return;
  }
  g_crash_guard_armed = 0;
  g_crash_sig = sig;
  g_crash_si_code = si != nullptr ? si->si_code : 0;
  g_crash_fault_addr =
      si != nullptr ? reinterpret_cast<unsigned long long>(si->si_addr) : 0ull;
  // 崩溃瞬间 PC/SP/FP（ucontext；各平台寄存器布局差异按宏分流，取不到置 0）
  unsigned long long pc = 0, sp = 0, fp = 0;
#if defined(__APPLE__) && defined(__aarch64__)
  if (uc != nullptr) {
    auto* m = static_cast<ucontext_t*>(uc)->uc_mcontext;
    pc = static_cast<unsigned long long>(m->__ss.__pc);
    sp = static_cast<unsigned long long>(m->__ss.__sp);
    fp = static_cast<unsigned long long>(m->__ss.__fp);
  }
#elif defined(__APPLE__) && defined(__x86_64__)
  if (uc != nullptr) {
    auto* m = static_cast<ucontext_t*>(uc)->uc_mcontext;
    pc = static_cast<unsigned long long>(m->__ss.__rip);
    sp = static_cast<unsigned long long>(m->__ss.__rsp);
    fp = static_cast<unsigned long long>(m->__ss.__rbp);
  }
#elif defined(__linux__) && defined(__aarch64__)
  if (uc != nullptr) {
    auto* m = &static_cast<ucontext_t*>(uc)->uc_mcontext;
    pc = static_cast<unsigned long long>(m->pc);
    sp = static_cast<unsigned long long>(m->sp);
    fp = static_cast<unsigned long long>(m->regs[29]);  // x29
  }
#elif defined(__linux__) && defined(__x86_64__)
  if (uc != nullptr) {
    auto* m = &static_cast<ucontext_t*>(uc)->uc_mcontext;
    pc = static_cast<unsigned long long>(m->gregs[REG_RIP]);
    sp = static_cast<unsigned long long>(m->gregs[REG_RSP]);
    fp = static_cast<unsigned long long>(m->gregs[REG_RBP]);
  }
#endif
  g_crash_pc = pc;
  g_crash_sp = sp;
  g_crash_fp = fp;
  // 栈顶摘要 + fp 形参溢出槽（区间归因位型）：SP 处为被中断帧的活跃栈，
  // 映射可读（fault 地址在他处）——定长 memcpy 读安全。fault 距 SP/FP 过近
  // （栈溢出类）则跳过读（防处理期内二次故障）。
  unsigned long long fa = g_crash_fault_addr;
  bool sp_safe = sp != 0 && (sp > fa ? sp - fa : fa - sp) > 0x10000ull;
  bool fp_safe = fp > 0x10000ull && (fp > fa ? fp - fa : fa - fp) > 0x10000ull;
  if (sp_safe) {
    std::memcpy(const_cast<unsigned long long*>(g_crash_stack), reinterpret_cast<const void*>(sp),
                sizeof(g_crash_stack));
  }
  if (fp_safe) {
    std::memcpy(const_cast<unsigned long long*>(g_crash_fparea),
                reinterpret_cast<const void*>(fp - 0x10), sizeof(g_crash_fparea));
  }
  siglongjmp(g_crash_guard_stack[g_crash_depth - 1], 1);
}

namespace {

// 信号/码名（可归因位型）：SEGV_ACCERR / BUS_ADRERR / ILL_ILLOPC / FPE_INTDIV ...
const char* CrashCodeName(int sig, int code) {
  if (sig == SIGSEGV) {
    switch (code) {
      case SEGV_MAPERR: return "SEGV_MAPERR";
      case SEGV_ACCERR: return "SEGV_ACCERR";
#ifdef SEGV_BNDERR
      case SEGV_BNDERR: return "SEGV_BNDERR";
#endif
#ifdef SEGV_PKUERR
      case SEGV_PKUERR: return "SEGV_PKUERR";
#endif
      default: break;
    }
  } else if (sig == SIGBUS) {
    switch (code) {
      case BUS_ADRALN: return "BUS_ADRALN";
      case BUS_ADRERR: return "BUS_ADRERR";
      case BUS_OBJERR: return "BUS_OBJERR";
      default: break;
    }
  } else if (sig == SIGILL) {
    switch (code) {
      case ILL_ILLOPC: return "ILL_ILLOPC";
      case ILL_ILLOPN: return "ILL_ILLOPN";
      case ILL_ILLADR: return "ILL_ILLADR";
      case ILL_ILLTRP: return "ILL_ILLTRP";
      default: break;
    }
  } else if (sig == SIGFPE) {
    switch (code) {
      case FPE_INTDIV: return "FPE_INTDIV";
      case FPE_INTOVF: return "FPE_INTOVF";
      default: break;
    }
  }
  return nullptr;
}

std::string HexAddr(unsigned long long v) {
  char b[24];
  std::snprintf(b, sizeof(b), "0x%llx", v);
  return b;
}

// 崩溃诊断串（siglongjmp 后调用）："SEGV_ACCERR @ 0x…（sig/si_code/pc/sp/fp 位型
// + 栈顶摘要 + fp 形参溢出槽——被扫区间 base/bytes 归因）"
std::string CrashDetailString() {
  int sig = g_crash_sig;
  int code = g_crash_si_code;
  const char* name = CrashCodeName(sig, code);
  std::string s = name != nullptr ? name : ("signal " + std::to_string(sig));
  s += " @ " + HexAddr(g_crash_fault_addr);
  s += " (sig=" + std::to_string(sig);
  s += " si_code=" + std::to_string(code);
  s += " pc=" + HexAddr(g_crash_pc);
  s += " sp=" + HexAddr(g_crash_sp);
  s += " fp=" + HexAddr(g_crash_fp);
  s += " fp[-2..-1]=" + HexAddr(g_crash_fparea[0]) + "," + HexAddr(g_crash_fparea[1]);
  // crash pc 所属模块（dladdr；guest .so / libdexvm.so 归因）
  Dl_info info{};
  if (::dladdr(reinterpret_cast<void*>(g_crash_pc), &info) != 0 && info.dli_fname != nullptr) {
    s += " module=" + std::string(info.dli_fname);
    unsigned long long off =
        g_crash_pc - reinterpret_cast<unsigned long long>(info.dli_fbase);
    s += "+" + HexAddr(off);
  }
  s += " stack:";
  for (unsigned long long w : g_crash_stack) s += " " + HexAddr(w);
  s += ")";
  return s;
}

}  // namespace

class CrashGuard {
 public:
  CrashGuard() {
    g_crash_sig = 0;
    g_crash_si_code = 0;
    g_crash_fault_addr = 0;
    g_crash_pc = 0;
    g_crash_sp = 0;
    // 信号备用栈（SA_ONSTACK）：guest 递归栈溢出也可捕获（无备用栈时溢出
    // 信号在已溢出的栈上投递 -> 处理器二次故障 -> sigchain 回落 SIG_DFL）。
    static char altstack[256 * 1024];
    stack_t ss{};
    ss.ss_sp = altstack;
    ss.ss_size = sizeof(altstack);
    ss.ss_flags = 0;
    ::sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = ExamCrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_ONSTACK | SA_SIGINFO;
    for (int s : {SIGSEGV, SIGBUS, SIGILL, SIGFPE}) {
      ::sigaction(s, &sa, &old_[count_++]);
    }
  }
  ~CrashGuard() {
    // 仅最外层析构清 armed（嵌套：内层 loadLibrary 护栏析构不得缴掉外层）
    if (g_crash_depth == 0) g_crash_guard_armed = 0;
    for (int i = 0; i < count_; i++) {
      ::sigaction(kSigs[i], &old_[i], nullptr);
    }
  }
  // true = 正常返回；false = 捕获崩溃（位型诊断记入 error：SEGV_ACCERR @ 0x…）
  bool Run(const std::function<bool(std::string*)>& fn, std::string* error) {
    if (g_crash_depth >= 4) {
      return fn(error);  // 嵌套超深：直通（最外层护栏兜底）
    }
    const int depth = g_crash_depth;
    g_crash_depth = depth + 1;
    if (sigsetjmp(g_crash_guard_stack[depth], 1) != 0) {
      g_crash_depth = depth;
      if (error != nullptr && error->empty()) {
        *error = "guest native crash: " + CrashDetailString();
      }
      return false;
    }
    g_crash_guard_armed = 1;
    bool ok = fn(error);
    if (depth == 0) g_crash_guard_armed = 0;
    g_crash_depth = depth;
    return ok;
  }

 private:
  static constexpr int kSigs[4] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};
  struct sigaction old_[4] = {};
  int count_ = 0;
};

// ---- APK 签名证书提取（PackageManager.getPackageInfo(GET_SIGNATURES) 语义：
//      APK Signature Scheme v2/v3 签名块 -> 首个 signer 证书 DER）----
bool ExtractApkSigningCert(const std::string& apk_path, std::vector<uint8_t>* cert,
                           std::string* error) {
  FILE* f = std::fopen(apk_path.c_str(), "rb");
  if (f == nullptr) {
    *error = "cannot open apk";
    return false;
  }
  auto fail = [&](const std::string& m) {
    std::fclose(f);
    *error = m;
    return false;
  };
  std::fseek(f, 0, SEEK_END);
  long fsize = std::ftell(f);
  if (fsize < 64) return fail("apk too small");
  size_t tail = fsize >= 65557 ? 65557 : static_cast<size_t>(fsize);
  std::fseek(f, fsize - static_cast<long>(tail), SEEK_SET);
  std::vector<uint8_t> buf(tail);
  if (std::fread(buf.data(), 1, tail, f) != tail) return fail("tail read failed");
  size_t eocd = std::string::npos;
  for (size_t i = tail - 22 + 1; i-- > 0;) {
    if (Rd32(buf.data() + i) == kZipSigEocd) {
      eocd = i;
      break;
    }
  }
  if (eocd == std::string::npos) return fail("EOCD not found");
  uint32_t cd_off = Rd32(buf.data() + eocd + 16);
  // 签名块尾部：size(8) + magic(16) 紧贴 central directory 之前
  if (cd_off < 24) return fail("no signing block");
  uint8_t trailer[24];
  std::fseek(f, static_cast<long>(cd_off) - 24, SEEK_SET);
  if (std::fread(trailer, 1, 24, f) != 24) return fail("signing trailer read failed");
  if (std::memcmp(trailer + 8, "APK Sig Block 42", 16) != 0) {
    return fail("no APK signing block (v2/v3)；v1 META-INF 证书解析后置");
  }
  uint64_t size_value = 0;
  std::memcpy(&size_value, trailer, 8);
  if (size_value < 24 || size_value > 64u * 1024u * 1024u) return fail("bad signing block size");
  uint64_t start = cd_off - (size_value + 8);
  std::vector<uint8_t> blk(static_cast<size_t>(size_value) + 8);
  std::fseek(f, static_cast<long>(start), SEEK_SET);
  if (std::fread(blk.data(), 1, blk.size(), f) != blk.size()) return fail("signing block read failed");
  std::fclose(f);
  // [size(8)][pairs][size(8)][magic(16)]：pair = len(8) id(4) value
  size_t off = 8;
  size_t end = blk.size() - 24;
  while (off + 12 <= end) {
    uint64_t pair_len = 0;
    std::memcpy(&pair_len, blk.data() + off, 8);
    if (pair_len < 4 || off + 8 + pair_len > end) break;
    uint32_t id = Rd32(blk.data() + off + 8);
    if (id == 0x7109871a || id == 0xf05368c0 || id == 0x1b93ad61) {  // v2/v3/v3.1
      const uint8_t* v = blk.data() + off + 12;
      size_t vlen = static_cast<size_t>(pair_len) - 4;
      // signers[0].signed_data.certificates[0]（全 length-prefixed）
      auto u32 = [&](size_t o) -> uint32_t {
        uint32_t x = 0;
        if (o + 4 <= vlen) std::memcpy(&x, v + o, 4);
        return x;
      };
      size_t sd_len = u32(8);
      const uint8_t* sd = v + 12;
      if (12 + sd_len > vlen) return fail("bad signer signed_data");
      uint32_t dig_len = u32(0);  // signed_data 内偏移 0
      std::memcpy(&dig_len, sd, 4);
      size_t certs_off = 4 + dig_len;
      uint32_t certs_len = 0;
      std::memcpy(&certs_len, sd + certs_off, 4);
      size_t c_off = certs_off + 4;
      size_t certs_end = c_off + certs_len;
      if (certs_end > sd_len) return fail("bad certificates region");
      if (c_off + 4 <= certs_end) {
        uint32_t clen = 0;
        std::memcpy(&clen, sd + c_off, 4);
        if (c_off + 4 + clen <= certs_end) {
          cert->assign(sd + c_off + 4, sd + c_off + 4 + clen);
          return true;
        }
      }
      return fail("no certificate in signer");
    }
    off += 8 + static_cast<size_t>(pair_len);
  }
  return fail("no v2/v3 signer found");
}

// ---- VM 语义面补全（M3 设备考卷引导面）：标准 Android framework API + 虚拟身份 ----
// 归因（jni trace）：壳链 JNI_OnLoad 语义需求为
//   ActivityThread.getSystemContext() -> ContextImpl.getPackageManager() ->
//   PackageManager.getPackageInfo(pkg, flags) -> PackageInfo.applicationInfo ->
//   ApplicationInfo.sourceDir/dataDir。
// 全部为标准 Android framework API 面（零壳特判）；身份/路径取真实布局：
// sourceDir=考卷 APK 路径、dataDir/nativeLibDir=宿主 files 布局、包名=manifest 身份
// （docs 29.2① 虚拟身份）。规范落点为 core/framework_builtins（M3 文件约束下在
// 设备考卷引导面注册，报告如实记录补全项）。
namespace semface {

using dexvm::runtime::ObjHeader;
using dexvm::runtime::ToIntrinsicSlot;
using dexvm::runtime::VArray;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;

// ActivityInfo 注册表（类名 dot -> 实例；getActivityInfo 查询 + applicationInfo
// 装配回填——单例就绪在 Complete 后段，故表置于命名空间级）
static std::map<std::string, dexvm::runtime::VObject*> s_activity_registry;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;
using dexvm::runtime::VString;

constexpr uint32_t kPublic = 0x1;
constexpr uint32_t kStatic = 0x8;

// 8 字节槽实例字段（builtin 布局约定，同 framework_builtins AddInstanceField）
void AddField(VClass* k, const char* name, const char* type, uint32_t byte_off) {
  dexvm::runtime::VField f;
  f.declaring = k;
  f.name = name;
  f.type_descriptor = type;
  f.access_flags = 0x2;  // private（读写经 JNI/VM 字段面）
  f.offset = byte_off;
  f.is_static = false;
  k->instance_fields.push_back(std::move(f));
}

// 方法骨架 + intrinsic 槽位（同 framework_builtins AddIntrinsicMethod 形态）
void AddIntrinsic(VClass* k, const char* name, std::vector<std::string> params,
                  const char* ret, uint32_t access, dexvm::runtime::IntrinsicFn fn) {
  // 同签名已存在（ApplyApiStubs 先登记的 ReturnNull/NoOp 补桩）时以真实实现
  // 覆盖：查找按方法表顺序命中第一条，追加会留下补桩优先的死实现
  // （PackageManager.getPackagesForUid 返回 null 的根因）。
  for (std::vector<dexvm::runtime::VMethod>* list :
       {&k->direct_methods, &k->virtual_methods}) {
    for (auto& m : *list) {
      if (m.name == name && m.param_types == params && m.return_type == ret) {
        m.declaring = k;
        m.access_flags = access;
        m.code = nullptr;
        m.native_fn = ToIntrinsicSlot(fn);
        return;
      }
    }
  }
  dexvm::runtime::VMethod m;
  m.declaring = k;
  m.name = name;
  m.param_types = std::move(params);
  m.return_type = ret;
  std::string shorty;
  shorty.push_back(ret[0] == 'L' || ret[0] == '[' ? 'L' : ret[0]);
  for (const auto& p : m.param_types) {
    shorty.push_back(p[0] == 'L' || p[0] == '[' ? 'L' : p[0]);
  }
  m.shorty = shorty;
  m.access_flags = access;
  m.code = nullptr;
  m.native_fn = ToIntrinsicSlot(fn);
  k->direct_methods.push_back(std::move(m));
}

VObject* AllocStr(dexvm::runtime::ClassLinker* linker, const std::string& s) {
  std::u16string u(s.begin(), s.end());
  return reinterpret_cast<VObject*>(linker->AllocString(u));
}

// java.io.File 实例（guest runtime.dex 类）：直写 path 字段（AOSP File.path），
// 供 Environment.get{Data,DownloadCache}Directory 等路径投影面复用。
VObject* MakeGuestFile(dexvm::runtime::VMState& state, const char* path) {
  VClass* file_cls = state.linker->FindClass("Ljava/io/File;");
  if (file_cls == nullptr) return nullptr;
  VObject* file = state.linker->AllocObject(file_cls);
  if (file == nullptr) return nullptr;
  ObjHeader* path_str = reinterpret_cast<ObjHeader*>(AllocStr(state.linker, path));
  for (VClass* k = file_cls; k != nullptr; k = k->super_class) {
    for (const auto& f : k->instance_fields) {
      if (f.name == "path") {
        std::memcpy(file->field_data() + f.offset, &path_str, sizeof(path_str));
        return file;
      }
    }
  }
  return file;  // 无 path 字段：仍返回裸对象（getPath 等调用面兜底）
}

void SetRef(VObject* o, uint32_t byte_off, ObjHeader* v) {
  std::memcpy(o->field_data() + byte_off, &v, sizeof(v));
}

// ---- intrinsics（单例经 g_exam；真实身份/路径）----
Value I_GetSystemContext(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  Value r{};
  r.ref = g_exam != nullptr ? g_exam->system_context : nullptr;
  if (r.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "getSystemContext: no context");
    *ok = false;
  }
  return r;
}
Value I_GetPackageManager(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  Value r{};
  r.ref = g_exam != nullptr ? g_exam->package_manager : nullptr;
  if (r.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "getPackageManager: no pm");
    *ok = false;
  }
  return r;
}
// PackageInfo.getLongVersionCode()（API 28+；versionCode 真值投影）
Value I_GetLongVersionCode(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  int32_t code = 0;
  if (self != nullptr && self->klass != nullptr) {
    for (VClass* k = self->klass; k != nullptr; k = k->super_class) {
      for (auto& f : k->instance_fields) {
        if (f.name == "versionCode") {
          std::memcpy(&code, self->field_data() + f.offset, sizeof(code));
          break;
        }
      }
    }
  }
  r.l = code;
  return r;
}

// ApplicationInfo.loadLabel(PackageManager)：应用标签投影（包名；宿主 UI 显示
// 面同源）。lib 无图标解码面，loadIcon/loadLogo 返回 null（调用方走默认图标）。
Value I_LoadLabel(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (self != nullptr) {
    std::memcpy(&r.ref, self->field_data() + 0, sizeof(r.ref));  // packageName @0
  }
  return r;
}
Value I_ReturnNullRef(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  return Value{};
}

// PackageManager.getPackagesForUid(int)：当前安装的考卷包（单包）
Value I_GetPackagesForUid(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  VClass* arr_cls = state.linker->FindClass("[Ljava/lang/String;");
  VArray* arr = arr_cls != nullptr ? state.linker->AllocArray(arr_cls, 1) : nullptr;
  if (arr == nullptr) {
    ThrowOOM(state, "getPackagesForUid");
    *ok = false;
    return r;
  }
  ObjHeader* name = g_exam != nullptr ? g_exam->package_name_str : nullptr;
  if (name != nullptr) {
    std::memcpy(arr->data(), &name, sizeof(name));
  } else {
    ObjHeader* nullv = nullptr;
    std::memcpy(arr->data(), &nullv, sizeof(nullv));
  }
  r.ref = arr;
  return r;
}

Value I_GetPackageInfo(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  Value r{};
  r.ref = g_exam != nullptr ? g_exam->package_info : nullptr;
  if (r.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "getPackageInfo: no info");
    *ok = false;
  }
  return r;
}
Value I_GetPackageName(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  Value r{};
  r.ref = g_exam != nullptr ? g_exam->package_name_str : nullptr;
  if (r.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "getPackageName: no name");
    *ok = false;
  }
  return r;
}
Value I_GetApplicationInfo(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  Value r{};
  r.ref = g_exam != nullptr ? g_exam->application_info : nullptr;
  if (r.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "getApplicationInfo: no info");
    *ok = false;
  }
  return r;
}
Value I_GetExternalStorageDir(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  Value r{};
  r.ref = g_exam != nullptr ? g_exam->external_storage : nullptr;
  if (r.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "getExternalStorageDirectory: none");
    *ok = false;
  }
  return r;
}
// android.os.Environment.getExternalStorageState()：外部存储挂载态投影。
// 实测账户初始化链（T_AccountManagerInit -> CloudIDHelper）以本方法为可用性
// 判据；缺失时 NoSuchMethodError 使账户体系停在 currentAccount=null，进而
// 阻断 Zonfig 配置拉取与首页内容。设备存储恒为 mounted（AOSP 语义）。
Value I_GetExternalStorageState(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(std::u16string(u"mounted")));
  return r;
}
// Environment.getDataDirectory()/getDownloadCacheDirectory()：设备数据区投影
//（CloudID 设备信息采集链消费；缺失时账号初始化在 currentAccount=null 处停摆）。
Value I_GetDataDirectory(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(MakeGuestFile(state, "/data"));
  return r;
}
Value I_GetDownloadCacheDirectory(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(MakeGuestFile(state, "/data/cache"));
  return r;
}
Value I_GetExternalStorageStateFile(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  return I_GetExternalStorageState(state, nullptr, nullptr, 0, ok);
}
// Environment.getExternalStoragePublicDirectory(String type)：外部存储公共目录
// 投影（/storage/emulated/0/<type>；宿主真实外部存储根）。
Value I_GetExternalStoragePublicDirectory(VMState& state, VObject*, const Value* args, size_t n,
                                          bool* ok) {
  *ok = true;
  Value r{};
  std::string path = "/storage/emulated/0";
  if (n >= 1 && args[0].ref != nullptr) {
    std::string dir = reinterpret_cast<VString*>(args[0].ref)->ToModifiedUtf8();
    if (!dir.empty()) path += "/" + dir;
  }
  r.ref = reinterpret_cast<ObjHeader*>(MakeGuestFile(state, path.c_str()));
  return r;
}
Value I_IsExternalStorageEmulated(VMState&, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.i = 1;
  return r;
}
Value I_IsExternalStorageRemovable(VMState&, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  return r;
}
// android.os.StatFs：设备存储统计面（CloudID 设备信息采集链消费）。实例字段
// 槽：path@0、blockCount@8、blockSize@16、availBlocks@24、freeBlocks@32。
// statvfs 失败按 AOSP 语义给零值（不抛，调用方只做容量展示/判空）。
uint64_t StatFsSlot(VObject* self, uint32_t off) {
  uint64_t v = 0;
  if (self != nullptr) std::memcpy(&v, self->field_data() + off, sizeof(v));
  return v;
}
Value I_StatFsInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (self == nullptr || n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "StatFs(path)");
    *ok = false;
    return r;
  }
  std::string path = reinterpret_cast<VString*>(args[0].ref)->ToModifiedUtf8();
  struct statvfs st{};
  uint64_t bc = 0, bs = 0, ab = 0, fb = 0;
  if (::statvfs(path.c_str(), &st) == 0) {
    bs = st.f_frsize != 0 ? static_cast<uint64_t>(st.f_frsize)
                          : static_cast<uint64_t>(st.f_bsize);
    bc = static_cast<uint64_t>(st.f_blocks);
    ab = static_cast<uint64_t>(st.f_bavail);
    fb = static_cast<uint64_t>(st.f_bfree);
  }
  ObjHeader* path_ref = args[0].ref;
  std::memcpy(self->field_data() + 0, &path_ref, sizeof(path_ref));
  std::memcpy(self->field_data() + 8, &bc, sizeof(bc));
  std::memcpy(self->field_data() + 16, &bs, sizeof(bs));
  std::memcpy(self->field_data() + 24, &ab, sizeof(ab));
  std::memcpy(self->field_data() + 32, &fb, sizeof(fb));
  return r;
}
Value I_StatFsBlockCount(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.i = static_cast<int32_t>(StatFsSlot(self, 8));
  return r;
}
Value I_StatFsBlockCountLong(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.l = static_cast<int64_t>(StatFsSlot(self, 8));
  return r;
}
Value I_StatFsBlockSize(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.i = static_cast<int32_t>(StatFsSlot(self, 16));
  return r;
}
Value I_StatFsBlockSizeLong(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.l = static_cast<int64_t>(StatFsSlot(self, 16));
  return r;
}
Value I_StatFsAvailableBlocks(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.i = static_cast<int32_t>(StatFsSlot(self, 24));
  return r;
}
Value I_StatFsAvailableBlocksLong(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.l = static_cast<int64_t>(StatFsSlot(self, 24));
  return r;
}
Value I_StatFsFreeBlocks(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.i = static_cast<int32_t>(StatFsSlot(self, 32));
  return r;
}
Value I_StatFsFreeBlocksLong(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.l = static_cast<int64_t>(StatFsSlot(self, 32));
  return r;
}
Value I_StatFsTotalBytes(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.l = static_cast<int64_t>(StatFsSlot(self, 8) * StatFsSlot(self, 16));
  return r;
}
Value I_StatFsAvailableBytes(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.l = static_cast<int64_t>(StatFsSlot(self, 24) * StatFsSlot(self, 16));
  return r;
}
Value I_StatFsFreeBytes(VMState&, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.l = static_cast<int64_t>(StatFsSlot(self, 32) * StatFsSlot(self, 16));
  return r;
}
Value I_SignatureToByteArray(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  Value r{};
  if (self == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "Signature.toByteArray on null");
    *ok = false;
    return r;
  }
  ObjHeader* arr = nullptr;
  std::memcpy(&arr, self->field_data() + 0, sizeof(arr));  // mSignature @0
  r.ref = arr;
  return r;
}

// 补全面注册 + 单例就绪。added：报告用补全清单。manifest = 值投影源
// （versionCode/versionName、ApplicationInfo.metaData 真值 = <meta-data> AXML 投影）。
void Complete(dexvm::runtime::VMState& state, dexvm::runtime::ClassLinker* linker,
              const dexvm::session::ManifestInfo& mi, const std::string& source_dir,
              const std::string& data_dir, const std::string& native_lib_dir,
              std::vector<std::string>* added) {
  const std::string& package_name = mi.package_name;
  const int32_t target_sdk = mi.target_sdk;
  VClass* object = linker->FindClass("Ljava/lang/Object;");
  VClass* context = linker->FindClass("Landroid/content/Context;");
  VClass* class_mirror = linker->FindClass("Ljava/lang/Class;");
  if (object == nullptr || context == nullptr) return;

  // Context 文件/存储面数据根（AOSP app data dir 语义；设备考语境 = 宿主 app
  // files 目录投影，与 ApplicationInfo.dataDir 同一落点——路径自洽）。
  dexvm::runtime::SetContextDataDir(data_dir);
  added->push_back("value Context data dir = " + data_dir);
  // 主题面：<application android:theme> 优先，其次 launcher Activity 的 theme
  {
    int32_t theme = mi.application_theme;
    if (theme == 0 && !mi.launcher_activity.empty()) {
      for (const auto& ae : mi.activities) {
        std::string n = ae.name;
        if (!n.empty() && n[0] == '.') n = package_name + n;
        if (n == mi.launcher_activity) {
          theme = ae.theme;
          break;
        }
      }
    }
    dexvm::runtime::SetAppThemeResId(theme);
    {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "app theme resId = 0x%08x", theme);
      ProgressAppend(std::string("value ") + buf);
      added->push_back(std::string("value ") + buf);
    }
  }

  auto make = [&](const char* desc, VClass* super, uint32_t extra) -> VClass* {
    void* mem = linker->heap()->Alloc(sizeof(VClass));
    if (mem == nullptr) return nullptr;
    VClass* k = new (mem) VClass();
    k->descriptor = desc;
    k->super_class = super;
    k->status = dexvm::runtime::ClassStatus::kInitialized;
    k->object_size = static_cast<uint32_t>(sizeof(ObjHeader)) + extra;
    k->klass = class_mirror;
    linker->RegisterBuiltinClass(k);
    added->push_back(std::string("class ") + desc);
    return k;
  };

  // android.content.pm.ApplicationInfo（String 身份/路径面，docs 29.2①；
  // targetSdkVersion = manifest 真实 targetSdk 投影；metaData = <meta-data> 投影槽；
  // flags = manifest 投影位（AOSP ApplicationInfo.FLAG_* 同值））
  VClass* ai = make("Landroid/content/pm/ApplicationInfo;", object, 112);
  if (ai != nullptr) {
    AddField(ai, "packageName", "Ljava/lang/String;", 0);
    AddField(ai, "sourceDir", "Ljava/lang/String;", 8);
    AddField(ai, "publicSourceDir", "Ljava/lang/String;", 16);
    AddField(ai, "dataDir", "Ljava/lang/String;", 24);
    AddField(ai, "nativeLibraryDir", "Ljava/lang/String;", 32);
    AddField(ai, "targetSdkVersion", "I", 40);
    AddField(ai, "metaData", "Landroid/os/Bundle;", 48);
    AddField(ai, "flags", "I", 56);
    AddField(ai, "minSdkVersion", "I", 64);
    AddField(ai, "uid", "I", 72);
    AddField(ai, "labelRes", "I", 80);
    // React Native SoLoader 初始化读 split 路径数组（单 APK 安装为 null；
    // 字段缺失会抛 NoSuchFieldError，中断 RN 引擎预初始化）。
    AddField(ai, "splitSourceDirs", "[Ljava/lang/String;", 88);
    AddField(ai, "splitPublicSourceDirs", "[Ljava/lang/String;", 96);
    AddField(ai, "splitNames", "[Ljava/lang/String;", 104);
    AddField(ai, "sharedLibraryFiles", "[Ljava/lang/String;", 112);
    AddIntrinsic(ai, "loadLabel", {"Landroid/content/pm/PackageManager;"},
                 "Ljava/lang/CharSequence;", kPublic, I_LoadLabel);
    AddIntrinsic(ai, "loadIcon", {"Landroid/content/pm/PackageManager;"},
                 "Landroid/graphics/drawable/Drawable;", kPublic, I_ReturnNullRef);
    AddIntrinsic(ai, "loadLogo", {"Landroid/content/pm/PackageManager;"},
                 "Landroid/graphics/drawable/Drawable;", kPublic, I_ReturnNullRef);
    added->push_back("method ApplicationInfo.{loadLabel,loadIcon,loadLogo}");
    added->push_back("field ApplicationInfo.{packageName,sourceDir,publicSourceDir,dataDir,"
                     "nativeLibraryDir,targetSdkVersion,metaData,flags}");
  }
  // android.content.pm.PackageInfo（versionCode/versionName = manifest 真值投影，
  // zonfig 请求构造链消费面）
  VClass* pi = make("Landroid/content/pm/PackageInfo;", object, 72);
  if (pi != nullptr) {
    AddField(pi, "packageName", "Ljava/lang/String;", 0);
    AddField(pi, "applicationInfo", "Landroid/content/pm/ApplicationInfo;", 8);
    AddField(pi, "signatures", "[Landroid/content/pm/Signature;", 16);
    AddField(pi, "versionName", "Ljava/lang/String;", 24);
    AddField(pi, "versionCode", "I", 32);
    AddField(pi, "requestedPermissions", "[Ljava/lang/String;", 40);
    AddField(pi, "lastUpdateTime", "J", 48);
    AddField(pi, "firstInstallTime", "J", 56);
    AddField(pi, "providers", "[Landroid/content/pm/ProviderInfo;", 64);
    AddIntrinsic(pi, "getLongVersionCode", {}, "J", kPublic, I_GetLongVersionCode);
    added->push_back("field PackageInfo.{packageName,applicationInfo,signatures,versionName,"
                     "versionCode}");
    added->push_back("method PackageInfo.getLongVersionCode()");
  }
  // android.content.pm.Signature（包签名证书面：真实 APK 签名块证书）
  VClass* sig_cls = make("Landroid/content/pm/Signature;", object, 8);
  if (sig_cls != nullptr) {
    AddField(sig_cls, "mSignature", "[B", 0);
    AddIntrinsic(sig_cls, "toByteArray", {}, "[B", kPublic, I_SignatureToByteArray);
    added->push_back("method Signature.toByteArray()");
  }
  // android.content.pm.PackageManager（framework 内建已有类面：resolveContentProvider/
  // getResourcesForApplication；此处补身份查询面 getPackageInfo/getApplicationInfo。
  // 已注册则复用（不重复登记类——同名双 VClass 会静默换布局，builtin 断言红线））
  VClass* pm = linker->FindClass("Landroid/content/pm/PackageManager;");
  if (pm == nullptr) pm = make("Landroid/content/pm/PackageManager;", object, 0);
  if (pm != nullptr) {
    AddIntrinsic(pm, "getPackageInfo",
                 {"Ljava/lang/String;", "I"}, "Landroid/content/pm/PackageInfo;", kPublic,
                 I_GetPackageInfo);
    AddIntrinsic(pm, "getPackagesForUid", {"I"}, "[Ljava/lang/String;", kPublic, I_GetPackagesForUid);
    AddIntrinsic(pm, "getApplicationInfo",
                 {"Ljava/lang/String;", "I"}, "Landroid/content/pm/ApplicationInfo;", kPublic,
                 I_GetApplicationInfo);
    added->push_back("method PackageManager.{getPackageInfo,getApplicationInfo}(String,int)");
  }
  // android.app.ContextImpl（AOSP：ContextImpl extends Context）
  VClass* ci = make("Landroid/app/ContextImpl;", context, 16);
  if (ci != nullptr) {
    AddIntrinsic(ci, "getPackageManager", {}, "Landroid/content/pm/PackageManager;", kPublic,
                 I_GetPackageManager);
    AddIntrinsic(ci, "getPackageName", {}, "Ljava/lang/String;", kPublic, I_GetPackageName);
    AddIntrinsic(ci, "getApplicationInfo", {}, "Landroid/content/pm/ApplicationInfo;", kPublic,
                 I_GetApplicationInfo);
    added->push_back("method ContextImpl.{getPackageManager,getPackageName,getApplicationInfo}");
  }
  // ActivityThread.getSystemContext()（补到既有 framework 内建类上）
  VClass* at = linker->FindClass("Landroid/app/ActivityThread;");
  if (at != nullptr) {
    AddIntrinsic(at, "getSystemContext", {}, "Landroid/app/ContextImpl;", kPublic,
                 I_GetSystemContext);
    added->push_back("method ActivityThread.getSystemContext()");
  }
  // android.os.Environment.getExternalStorageDirectory()（标准 API：外部存储根）
  VClass* env_cls = make("Landroid/os/Environment;", object, 0);
  if (env_cls != nullptr) {
    // AOSP Environment 公开常量（目录名/MEDIA_* 挂载态；业务码直读静态字段）
    auto add_env_str = [&](const char* name, const char* value) {
      dexvm::runtime::VField f;
      f.declaring = env_cls;
      f.name = name;
      f.type_descriptor = "Ljava/lang/String;";
      f.access_flags = kPublic | kStatic;
      f.offset = static_cast<uint32_t>(env_cls->static_storage.size());
      f.is_static = true;
      env_cls->static_fields.push_back(std::move(f));
      env_cls->static_storage.push_back(Value{});
      env_cls->static_storage.back().ref =
          reinterpret_cast<ObjHeader*>(AllocStr(linker, value));
    };
    add_env_str("MEDIA_MOUNTED", "mounted");
    add_env_str("MEDIA_UNMOUNTED", "unmounted");
    add_env_str("MEDIA_REMOVED", "removed");
    add_env_str("MEDIA_BAD_REMOVAL", "bad_removal");
    add_env_str("MEDIA_CHECKING", "checking");
    add_env_str("DIRECTORY_MUSIC", "Music");
    add_env_str("DIRECTORY_PODCASTS", "Podcasts");
    add_env_str("DIRECTORY_RINGTONES", "Ringtones");
    add_env_str("DIRECTORY_ALARMS", "Alarms");
    add_env_str("DIRECTORY_NOTIFICATIONS", "Notifications");
    add_env_str("DIRECTORY_PICTURES", "Pictures");
    add_env_str("DIRECTORY_MOVIES", "Movies");
    add_env_str("DIRECTORY_DOWNLOADS", "Download");
    add_env_str("DIRECTORY_DCIM", "DCIM");
    add_env_str("DIRECTORY_DOCUMENTS", "Documents");
    AddIntrinsic(env_cls, "getExternalStorageDirectory", {}, "Ljava/io/File;", kStatic,
                 I_GetExternalStorageDir);
    AddIntrinsic(env_cls, "getExternalStoragePublicDirectory", {"Ljava/lang/String;"},
                 "Ljava/io/File;", kStatic, I_GetExternalStoragePublicDirectory);
    added->push_back("field Environment.{DIRECTORY_*,MEDIA_*}");
    AddIntrinsic(env_cls, "getExternalStorageState", {}, "Ljava/lang/String;", kStatic,
                 I_GetExternalStorageState);
    AddIntrinsic(env_cls, "getExternalStorageState", {"Ljava/io/File;"}, "Ljava/lang/String;",
                 kStatic, I_GetExternalStorageStateFile);
    AddIntrinsic(env_cls, "getDataDirectory", {}, "Ljava/io/File;", kStatic, I_GetDataDirectory);
    AddIntrinsic(env_cls, "getDownloadCacheDirectory", {}, "Ljava/io/File;", kStatic,
                 I_GetDownloadCacheDirectory);
    AddIntrinsic(env_cls, "isExternalStorageEmulated", {}, "Z", kStatic,
                 I_IsExternalStorageEmulated);
    AddIntrinsic(env_cls, "isExternalStorageRemovable", {}, "Z", kStatic,
                 I_IsExternalStorageRemovable);
    added->push_back("method Environment.getExternalStorageDirectory()");
    added->push_back("method Environment.{getExternalStorageState,getDataDirectory,"
                     "getDownloadCacheDirectory,isExternalStorage*}");
  }
  // android.os.StatFs（存储统计投影；CloudID 设备信息采集链 new StatFs(path)）
  VClass* statfs_cls = make("Landroid/os/StatFs;", object, 40);
  if (statfs_cls != nullptr) {
    AddField(statfs_cls, "path", "Ljava/lang/String;", 0);
    AddField(statfs_cls, "blockCount", "J", 8);
    AddField(statfs_cls, "blockSize", "J", 16);
    AddField(statfs_cls, "availBlocks", "J", 24);
    AddField(statfs_cls, "freeBlocks", "J", 32);
    AddIntrinsic(statfs_cls, "<init>", {"Ljava/lang/String;"}, "V", kPublic, I_StatFsInit);
    AddIntrinsic(statfs_cls, "restat", {"Ljava/lang/String;"}, "V", kPublic, I_StatFsInit);
    AddIntrinsic(statfs_cls, "getBlockCount", {}, "I", kPublic, I_StatFsBlockCount);
    AddIntrinsic(statfs_cls, "getBlockCountLong", {}, "J", kPublic, I_StatFsBlockCountLong);
    AddIntrinsic(statfs_cls, "getBlockSize", {}, "I", kPublic, I_StatFsBlockSize);
    AddIntrinsic(statfs_cls, "getBlockSizeLong", {}, "J", kPublic, I_StatFsBlockSizeLong);
    AddIntrinsic(statfs_cls, "getAvailableBlocks", {}, "I", kPublic, I_StatFsAvailableBlocks);
    AddIntrinsic(statfs_cls, "getAvailableBlocksLong", {}, "J", kPublic,
                 I_StatFsAvailableBlocksLong);
    AddIntrinsic(statfs_cls, "getFreeBlocks", {}, "I", kPublic, I_StatFsFreeBlocks);
    AddIntrinsic(statfs_cls, "getFreeBlocksLong", {}, "J", kPublic, I_StatFsFreeBlocksLong);
    AddIntrinsic(statfs_cls, "getTotalBytes", {}, "J", kPublic, I_StatFsTotalBytes);
    AddIntrinsic(statfs_cls, "getAvailableBytes", {}, "J", kPublic, I_StatFsAvailableBytes);
    AddIntrinsic(statfs_cls, "getFreeBytes", {}, "J", kPublic, I_StatFsFreeBytes);
    added->push_back("class android.os.StatFs");
  }
  // java.lang.Void（TYPE = void 原始类镜像；jre-excluded VM 内建面，
  // runtime.dex 未含——guest 反射常用 Void.TYPE）
  {
    VClass* void_cls = make("Ljava/lang/Void;", object, 0);
    VClass* pv = linker->PrimitiveClass('V');
    if (pv != nullptr && pv->klass == nullptr && class_mirror != nullptr) {
      pv->klass = class_mirror;  // 镜像面：guest invoke-virtual on mirror
    }
    if (void_cls != nullptr && pv != nullptr) {
      dexvm::runtime::VField f;
      f.declaring = void_cls;
      f.name = "TYPE";
      f.type_descriptor = "Ljava/lang/Class;";
      f.access_flags = kPublic | kStatic;
      f.offset = 0;
      f.is_static = true;
      void_cls->static_fields.push_back(std::move(f));
      void_cls->static_storage.assign(1, Value{});
      void_cls->static_storage[0].ref = reinterpret_cast<ObjHeader*>(pv);
      added->push_back("class java.lang.Void + static TYPE = void class mirror");
    }
  }

  // android.content.pm.ServiceInfo（getServiceInfo 值投影面；manifest <service>
  // 全字段 + <meta-data> 投影——AGC ServiceDiscovery 消费面同型）。
  // 注册表：组件类名（dot）-> ServiceInfo；getServiceInfo(ComponentName,int) 查询。
  // AOSP：无主抛 PackageManager$NameNotFoundException（AGC 等调用方按此语义捕获）。
  {
    std::map<std::string, VObject*> service_registry;
    VClass* si_cls = make("Landroid/content/pm/ServiceInfo;", object, 48);
    if (si_cls != nullptr) {
      AddField(si_cls, "name", "Ljava/lang/String;", 0);
      AddField(si_cls, "packageName", "Ljava/lang/String;", 8);
      AddField(si_cls, "metaData", "Landroid/os/Bundle;", 16);
      AddField(si_cls, "permission", "Ljava/lang/String;", 24);
      AddField(si_cls, "processName", "Ljava/lang/String;", 32);
      AddField(si_cls, "exported", "Z", 40);
      added->push_back("field ServiceInfo.{name,packageName,metaData,permission,processName,"
                       "exported}");
      auto expand_name = [](const std::string& pkg, const std::string& n) {
        if (n.empty()) return n;
        if (n[0] == '.') return pkg + n;
        if (n.find('.') == std::string::npos) return pkg + "." + n;
        return n;
      };
      for (const auto& se : mi.services) {
        VObject* info = linker->AllocObject(si_cls);
        if (info == nullptr) continue;
        // 字段区清零（AllocObject 不保证清零；条件写槽外必须零值=合法 null ref）
        std::memset(info->field_data(), 0, 48);
        std::string full = expand_name(package_name, se.name);
        SetRef(info, 0, reinterpret_cast<ObjHeader*>(AllocStr(linker, full)));
        SetRef(info, 8, reinterpret_cast<ObjHeader*>(AllocStr(linker, package_name)));
        if (!se.meta_data.empty()) {
          VObject* meta = dexvm::framework::AssembleMetaBundle(state, se.meta_data);
          SetRef(info, 16, reinterpret_cast<ObjHeader*>(meta));
        }
        if (!se.permission.empty()) {
          SetRef(info, 24, reinterpret_cast<ObjHeader*>(AllocStr(linker, se.permission)));
        }
        std::string proc =
            se.process_name.empty() ? package_name
            : (se.process_name[0] == ':' ? package_name + se.process_name : se.process_name);
        SetRef(info, 32, reinterpret_cast<ObjHeader*>(AllocStr(linker, proc)));
        uint64_t slot = se.exported ? 1 : 0;
        std::memcpy(info->field_data() + 40, &slot, sizeof(slot));
        service_registry[full] = info;
      }
    }
    // GC 保活：注册表对象钉在 ServiceInfo 静态槽（gc.cc 扫 static_storage；
    // 原生 std::map 节点不在 GC 视野——不钉 = 回收后悬挂读 = dirty reference）。
    if (si_cls != nullptr && !service_registry.empty()) {
      VClass* obj_arr = linker->FindArrayClass(linker->FindClass("Ljava/lang/Object;"));
      VArray* pinned = obj_arr != nullptr
                          ? linker->AllocArray(obj_arr, static_cast<int32_t>(service_registry.size()))
                          : nullptr;
      if (pinned != nullptr) {
        int32_t i = 0;
        for (const auto& kv : service_registry) {
          ObjHeader* ref = reinterpret_cast<ObjHeader*>(kv.second);
          std::memcpy(pinned->data() + static_cast<size_t>(i) * 8, &ref, sizeof(ref));
          i++;
        }
        dexvm::runtime::VField f;
        f.declaring = si_cls;
        f.name = "sPinnedRegistry";
        f.type_descriptor = "[Ljava/lang/Object;";
        f.access_flags = kPublic | kStatic;
        f.offset = 0;
        f.is_static = true;
        si_cls->static_fields.push_back(std::move(f));
        si_cls->static_storage.assign(1, Value{});
        si_cls->static_storage[0].ref = reinterpret_cast<ObjHeader*>(pinned);
      }
    }
    // getServiceInfo(ComponentName,int)：mClass 查询（dot 名）；无主抛
    // NameNotFoundException（AOSP 同口径）。
    static std::map<std::string, VObject*>* s_registry = nullptr;
    s_registry = new std::map<std::string, VObject*>(std::move(service_registry));
    VClass* pm2 = linker->FindClass("Landroid/content/pm/PackageManager;");
    if (pm2 != nullptr) {
      AddIntrinsic(pm2, "getServiceInfo",
                   {"Landroid/content/ComponentName;", "I"}, "Landroid/content/pm/ServiceInfo;",
                   kPublic,
                   [](VMState& st, VObject*, const Value* a, size_t na, bool* ok) -> Value {
                     Value r{};
                     if (na < 1 || a[0].ref == nullptr) {
                       st.ThrowNew("Ljava/lang/NullPointerException;", "component");
                       *ok = false;
                       return r;
                     }
                     VObject* cn = reinterpret_cast<VObject*>(a[0].ref);
                     ObjHeader* cls_ref = nullptr;
                     for (VClass* k = cn->klass; k != nullptr && cls_ref == nullptr;
                          k = k->super_class) {
                       for (auto& f : k->instance_fields) {
                         if (f.name == "mClass") {
                           std::memcpy(&cls_ref, cn->field_data() + f.offset, sizeof(cls_ref));
                           break;
                         }
                       }
                     }
                     std::string cls_name;
                     if (cls_ref != nullptr) {
                       cls_name = reinterpret_cast<VString*>(cls_ref)->ToModifiedUtf8();
                     }
                     auto it = s_registry != nullptr ? s_registry->find(cls_name)
                                                     : std::map<std::string, VObject*>::iterator();
                     if (s_registry != nullptr && it != s_registry->end()) {
                       r.ref = it->second;
                       *ok = true;
                       return r;
                     }
                     st.ThrowNew("Landroid/content/pm/PackageManager$NameNotFoundException;",
                                 cls_name);
                     *ok = false;
                     return r;
                   });
      added->push_back("method PackageManager.getServiceInfo(ComponentName,int)");
    }
  }

  // android.content.pm.ActivityInfo（getActivityInfo 值投影面；manifest
  // <activity>/<activity-alias> 全字段 + <meta-data>——androidx 兼容初始化/
  // 壳组件查询链；类由 framework_builtins 注册，此处只做值装配）。
  {
    using CV = dexvm::runtime::VObject;
    std::map<std::string, CV*> activity_registry;
    VClass* acls = linker->FindClass("Landroid/content/pm/ActivityInfo;");
    if (acls != nullptr) {
      auto expand_name = [](const std::string& pkg, const std::string& n) {
        if (n.empty()) return n;
        if (n[0] == '.') return pkg + n;
        if (n.find('.') == std::string::npos) return pkg + "." + n;
        return n;
      };
      for (const auto& ae : mi.activities) {
        CV* info = linker->AllocObject(acls);
        if (info == nullptr) continue;
        std::memset(info->field_data(), 0, acls->object_size - sizeof(dexvm::runtime::ObjHeader));
        std::string full = expand_name(package_name, ae.name);
        SetRef(info, 0, reinterpret_cast<ObjHeader*>(AllocStr(linker, full)));
        SetRef(info, 8, reinterpret_cast<ObjHeader*>(AllocStr(linker, package_name)));
        if (!ae.meta_data.empty()) {
          VObject* meta = dexvm::framework::AssembleMetaBundle(state, ae.meta_data);
          SetRef(info, 56, reinterpret_cast<ObjHeader*>(meta));  // PackageItemInfo.metaData
        }
        std::string proc =
            ae.process_name.empty() ? package_name
            : (ae.process_name[0] == ':' ? package_name + ae.process_name : ae.process_name);
        SetRef(info, 16, reinterpret_cast<ObjHeader*>(AllocStr(linker, proc)));
        uint64_t exp = ae.exported ? 1 : 0;
        std::memcpy(info->field_data() + 64, &exp, sizeof(exp));
        uint64_t en = ae.enabled ? 1 : 0;
        std::memcpy(info->field_data() + 72, &en, sizeof(en));
        uint64_t th = static_cast<uint64_t>(static_cast<uint32_t>(ae.theme));
        std::memcpy(info->field_data() + 80, &th, sizeof(th));
        uint64_t lm = static_cast<uint64_t>(static_cast<uint32_t>(ae.launch_mode));
        std::memcpy(info->field_data() + 88, &lm, sizeof(lm));
        uint64_t cc = static_cast<uint64_t>(static_cast<uint32_t>(ae.config_changes));
        std::memcpy(info->field_data() + 92, &cc, sizeof(cc));
        // parentActivityName（NavUtils.getParentActivityName 消费面；缺省 null）
        if (!ae.parent_name.empty()) {
          SetRef(info, 96, reinterpret_cast<ObjHeader*>(
                               AllocStr(linker, expand_name(package_name, ae.parent_name))));
        }
        activity_registry[full] = info;
      }
      // GC 保活：注册表钉在 ActivityInfo 静态槽（同 ServiceInfo 口径）
      if (!activity_registry.empty()) {
        VClass* obj_arr = linker->FindArrayClass(linker->FindClass("Ljava/lang/Object;"));
        VArray* pinned =
            obj_arr != nullptr
                ? linker->AllocArray(obj_arr, static_cast<int32_t>(activity_registry.size()))
                : nullptr;
        if (pinned != nullptr) {
          int32_t i = 0;
          for (const auto& kv : activity_registry) {
            ObjHeader* ref = reinterpret_cast<ObjHeader*>(kv.second);
            std::memcpy(pinned->data() + static_cast<size_t>(i) * 8, &ref, sizeof(ref));
            i++;
          }
          dexvm::runtime::VField f;
          f.declaring = acls;
          f.name = "sPinnedRegistry";
          f.type_descriptor = "[Ljava/lang/Object;";
          f.access_flags = kPublic | kStatic;
          f.offset = 0;
          f.is_static = true;
          acls->static_fields.push_back(std::move(f));
          acls->static_storage.assign(1, Value{});
          acls->static_storage[0].ref = reinterpret_cast<ObjHeader*>(pinned);
        }
      }
      added->push_back("value ActivityInfo registry (" +
                       std::to_string(activity_registry.size()) + " activities)");
    }
    // getActivityInfo(ComponentName,int)：无主抛 NameNotFoundException（AOSP 同口径）
    s_activity_registry = std::move(activity_registry);
#if defined(__ANDROID__)
    // 诊断（临时）：manifest activity 注册表快照（MainActivity onCreate 自查询面归因）。
    __android_log_print(ANDROID_LOG_INFO, "DexVmPm", "activity registry=%zu",
                        s_activity_registry.size());
    {
      size_t n = 0;
      for (const auto& kv : s_activity_registry) {
        if (n++ >= 40) break;
        __android_log_print(ANDROID_LOG_INFO, "DexVmPm", "  act %s", kv.first.c_str());
      }
    }
#endif
    VClass* pm3 = linker->FindClass("Landroid/content/pm/PackageManager;");
    if (pm3 != nullptr) {
      AddIntrinsic(pm3, "getActivityInfo",
                   {"Landroid/content/ComponentName;", "I"},
                   "Landroid/content/pm/ActivityInfo;", kPublic,
                   [](VMState& st, VObject*, const Value* a, size_t na, bool* ok) -> Value {
                     Value r{};
                     if (na < 1 || a[0].ref == nullptr) {
                       st.ThrowNew("Ljava/lang/NullPointerException;", "component");
                       *ok = false;
                       return r;
                     }
                     VObject* cn = reinterpret_cast<VObject*>(a[0].ref);
                     std::string cls_name;
                     for (VClass* k = cn->klass; k != nullptr && cls_name.empty();
                          k = k->super_class) {
                       for (auto& f : k->instance_fields) {
                         if (f.name == "mClass") {
                           ObjHeader* s = nullptr;
                           std::memcpy(&s, cn->field_data() + f.offset, sizeof(s));
                           if (s != nullptr) {
                             cls_name = reinterpret_cast<VString*>(s)->ToModifiedUtf8();
                           }
                           break;
                         }
                       }
                     }
                     auto it = s_activity_registry.find(cls_name);
                     if (it != s_activity_registry.end()) {
                       r.ref = it->second;
                       *ok = true;
                       return r;
                     }
#if defined(__ANDROID__)
                     __android_log_print(ANDROID_LOG_INFO, "DexVmPm",
                                         "getActivityInfo miss cls=%s registry=%zu",
                                         cls_name.c_str(), s_activity_registry.size());
#endif
                     st.ThrowNew("Landroid/content/pm/PackageManager$NameNotFoundException;",
                                 "Unable to find explicit activity class {" + cls_name + "}");
                     *ok = false;
                     return r;
                   });
      added->push_back("method PackageManager.getActivityInfo(ComponentName,int)");
    }
  }

  // 单例装配（真实布局：考卷 APK 即安装 APK；files 布局即宿主数据目录）
  if (ai == nullptr || pi == nullptr || pm == nullptr || ci == nullptr) return;
  ExamDeviceContext* ctx = g_exam;
  if (ctx == nullptr) return;
  ctx->application_info = linker->AllocObject(ai);
  ctx->package_info = linker->AllocObject(pi);
  // 字段区清零（AllocObject 不保证清零；未写槽必须零值 = 合法 null/0）
  if (ctx->application_info != nullptr) std::memset(ctx->application_info->field_data(), 0, 88);
  if (ctx->package_info != nullptr) std::memset(ctx->package_info->field_data(), 0, 72);
  ctx->package_manager = linker->AllocObject(pm);
  ctx->system_context = linker->AllocObject(ci);
  // Context 服务桥（AOSP 同 PM 全 Context 可得）：Context.getPackageManager()
  // 基础面全局单例落点（ContextImpl 覆写仍虚分发优先）。ApplicationInfo 同口径。
  dexvm::runtime::SetContextPackageManager(ctx->package_manager);
  dexvm::runtime::SetContextApplicationInfo(ctx->application_info);
  // ActivityInfo.applicationInfo（ComponentInfo@48）= 同源单例（AOSP
  // ComponentInfo.applicationInfo 恒非空；getActivityInfo 消费方直接取用）
  for (auto& kv : s_activity_registry) {
    SetRef(kv.second, 48, reinterpret_cast<ObjHeader*>(ctx->application_info));
  }
  added->push_back("value Context.{getPackageManager,getApplicationInfo}() = system singletons");
  {
    // Context.getResources() = app 资源表（docs 20 章 Resources 面；考卷 APK 的
    // resources.arsc 资源表——FileProvider meta-data 消费链取用）
    dexvm::runtime::VObject* app_res =
        dexvm::runtime::AllocResources(state, source_dir, package_name);
    if (app_res != nullptr) {
      dexvm::runtime::SetContextResources(app_res);
      added->push_back("value Context.getResources() = Resources(" + source_dir + ")");
    }
  }
  ctx->package_name_str = AllocStr(linker, package_name);
  if (ctx->application_info == nullptr || ctx->package_info == nullptr) return;
  SetRef(ctx->application_info, 0, reinterpret_cast<ObjHeader*>(AllocStr(linker, package_name)));
  SetRef(ctx->application_info, 8, reinterpret_cast<ObjHeader*>(AllocStr(linker, source_dir)));
  SetRef(ctx->application_info, 16, reinterpret_cast<ObjHeader*>(AllocStr(linker, source_dir)));
  // ApplicationInfo.dataDir = 虚拟身份数据目录（<hostData>/<packageName>：
  // 容器虚拟身份面 docs 29.2①；真实文件 IO 仍走 Context data dir=宿主 files）
  std::string virtual_data_dir = data_dir;
  if (!package_name.empty()) virtual_data_dir += "/" + package_name;
  SetRef(ctx->application_info, 24,
         reinterpret_cast<ObjHeader*>(AllocStr(linker, virtual_data_dir)));
  SetRef(ctx->application_info, 32,
         reinterpret_cast<ObjHeader*>(AllocStr(linker, native_lib_dir)));
  {
    // targetSdkVersion = manifest 真实 targetSdk（虚拟身份如实投影）
    uint64_t tslot = static_cast<uint64_t>(static_cast<uint32_t>(target_sdk));
    std::memcpy(ctx->application_info->field_data() + 40, &tslot, sizeof(tslot));
    added->push_back("value ApplicationInfo.targetSdkVersion = " + std::to_string(target_sdk));
    uint64_t mslot = static_cast<uint64_t>(static_cast<uint32_t>(mi.min_sdk));
    std::memcpy(ctx->application_info->field_data() + 64, &mslot, sizeof(mslot));
    added->push_back("value ApplicationInfo.minSdkVersion = " + std::to_string(mi.min_sdk));
    uint64_t uslot = static_cast<uint64_t>(static_cast<uint32_t>(::getuid()));
    std::memcpy(ctx->application_info->field_data() + 72, &uslot, sizeof(uslot));
  }
  {
    // ApplicationInfo.flags = manifest 投影位（AOSP ApplicationInfo.FLAG_* 同值；
    // 用户安装考卷非系统包：FLAG_SYSTEM 不置位；其余安装态位不伪造）：
    // hasCode -> FLAG_HAS_CODE、debuggable -> FLAG_DEBUGGABLE、
    // largeHeap -> FLAG_LARGE_HEAP（AOSP parseBaseApplication 同口径）
    constexpr uint32_t kFlagDebuggable = 0x2;
    constexpr uint32_t kFlagHasCode = 0x4;
    constexpr uint32_t kFlagLargeHeap = 0x1000000;
    uint64_t flags = 0;
    if (mi.has_code) flags |= kFlagHasCode;
    if (mi.debuggable) flags |= kFlagDebuggable;
    if (mi.large_heap) flags |= kFlagLargeHeap;
    std::memcpy(ctx->application_info->field_data() + 56, &flags, sizeof(flags));
    char fbuf[32];
    std::snprintf(fbuf, sizeof(fbuf), "0x%llx", static_cast<unsigned long long>(flags));
    added->push_back(std::string("value ApplicationInfo.flags = ") + fbuf +
                     " (hasCode=" + std::to_string(mi.has_code ? 1 : 0) +
                     ", debuggable=" + std::to_string(mi.debuggable ? 1 : 0) +
                     ", largeHeap=" + std::to_string(mi.large_heap ? 1 : 0) + ")");
  }
  {
    // metaData 真值 = <meta-data> AXML 投影（AOSP parseMetaData 同型：typed
    // value 分型入 Bundle）；无 <meta-data> = null（AOSP 同口径）
    VObject* bundle = dexvm::framework::AssembleMetaBundle(state, mi.application_meta_data);
    if (bundle != nullptr) {
      SetRef(ctx->application_info, 48, reinterpret_cast<ObjHeader*>(bundle));
      added->push_back("value ApplicationInfo.metaData = Bundle(" +
                       std::to_string(mi.application_meta_data.size()) + " entries)");
    } else if (!mi.application_meta_data.empty()) {
      added->push_back("value ApplicationInfo.metaData = null (Bundle assembly failed)");
    } else {
      added->push_back("value ApplicationInfo.metaData = null (no <meta-data>)");
    }
  }
  SetRef(ctx->package_info, 0, reinterpret_cast<ObjHeader*>(AllocStr(linker, package_name)));
  SetRef(ctx->package_info, 8, reinterpret_cast<ObjHeader*>(ctx->application_info));
  {
    // PackageInfo.{firstInstallTime,lastUpdateTime} = APK 文件 mtime（毫秒；
    // 文件不可达 = 0）。CloudID/设备信息链按安装时间排序/去重消费。
    struct stat st {};
    int64_t mtime_ms = 0;
    if (::stat(source_dir.c_str(), &st) == 0) {
      mtime_ms = static_cast<int64_t>(st.st_mtime) * 1000;
    }
    std::memcpy(ctx->package_info->field_data() + 48, &mtime_ms, sizeof(mtime_ms));
    std::memcpy(ctx->package_info->field_data() + 56, &mtime_ms, sizeof(mtime_ms));
    added->push_back("value PackageInfo.{firstInstallTime,lastUpdateTime} = " +
                     std::to_string(mtime_ms));
  }
  {
    // PackageInfo.requestedPermissions = 空 String[]（非 null：未带
    // GET_PERMISSIONS 位时 AOSP 为 null，但调用方常直接遍历——空表两条路径
    // 语义等价且不 NPE）。
    VClass* str_cls = linker->FindClass("Ljava/lang/String;");
    VClass* arr_cls = str_cls != nullptr ? linker->FindArrayClass(str_cls) : nullptr;
    VArray* arr = arr_cls != nullptr ? linker->AllocArray(arr_cls, 0) : nullptr;
    if (arr != nullptr) {
      SetRef(ctx->package_info, 40, reinterpret_cast<ObjHeader*>(arr));
    }
  }
  // PackageInfo.providers 保持 null（AOSP：未带 GET_PROVIDERS 位即 null）。
  {
    // PackageInfo.versionCode/versionName = manifest 真值（zonfig 请求构造链引用）
    dexvm::framework::ProjectPackageInfo(state, ctx->package_info, mi);
    added->push_back("value PackageInfo.versionCode = " + std::to_string(mi.version_code) +
                     ", versionName = " + (mi.version_name.empty() ? "(null)" : mi.version_name));
  }

  // PackageInfo.signatures = [Signature(真实 APK 签名块证书)]（包检查语义：考卷即
  // 真实 zhihu APK，签名证书为该包自身签名——虚拟身份如实投影，非伪造）
  if (sig_cls != nullptr) {
    std::vector<uint8_t> cert;
    std::string cerr;
    if (ExtractApkSigningCert(source_dir, &cert, &cerr) && !cert.empty()) {
      VObject* sig_obj = linker->AllocObject(sig_cls);
      VClass* byte_cls = linker->PrimitiveClass('B');
      VClass* barr_cls = byte_cls != nullptr ? linker->FindArrayClass(byte_cls) : nullptr;
      VArray* cert_arr =
          barr_cls != nullptr ? linker->AllocArray(barr_cls, static_cast<int32_t>(cert.size()))
                              : nullptr;
      if (sig_obj != nullptr && cert_arr != nullptr) {
        for (size_t i = 0; i < cert.size(); i++) {
          uint64_t slot = cert[i];
          std::memcpy(cert_arr->data() + i * 8, &slot, 8);
        }
        SetRef(sig_obj, 0, reinterpret_cast<ObjHeader*>(cert_arr));
        VClass* sarr_cls = linker->FindArrayClass(sig_cls);
        VArray* sigs = sarr_cls != nullptr ? linker->AllocArray(sarr_cls, 1) : nullptr;
        if (sigs != nullptr) {
          uint64_t slot = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(sig_obj));
          std::memcpy(sigs->data(), &slot, 8);
          SetRef(ctx->package_info, 16, reinterpret_cast<ObjHeader*>(sigs));
          added->push_back("value PackageInfo.signatures = [Signature(real APK cert, " +
                           std::to_string(cert.size()) + " bytes DER)]");
        }
      }
    } else {
      added->push_back("signatures: cert extract failed: " + cerr);
    }
  }

  // java.io.File 实例（guest 类，runtime.dex）：外部存储根，真实设备路径。
  // 经 guest <init>(String) 构造（resolvePath 语义）；失败回落直写 path 字段。
  VClass* file_cls = linker->FindClass("Ljava/io/File;");
  if (file_cls != nullptr) {
    VObject* file = linker->AllocObject(file_cls);
    bool built = false;
    if (file != nullptr) {
      dexvm::runtime::VMethod* ctor = nullptr;
      for (auto& m : file_cls->direct_methods) {
        if (m.name == "<init>" && m.param_types.size() == 1 &&
            m.param_types[0] == "Ljava/lang/String;") {
          ctor = &m;
          break;
        }
      }
      if (ctor != nullptr) {
        Value args[2] = {};
        args[0].ref = file;
        args[1].ref = reinterpret_cast<ObjHeader*>(AllocStr(linker, "/storage/emulated/0"));
        Value ret{};
        built = dexvm::runtime::InvokeMethod(state, ctor, args, 2, &ret) &&
                !state.HasPendingException();
        state.ClearException();
      }
      if (!built) {
        for (const auto& f : file_cls->instance_fields) {
          if (f.name == "path") {
            SetRef(file, f.offset, reinterpret_cast<ObjHeader*>(AllocStr(linker, "/storage/emulated/0")));
            built = true;
            break;
          }
        }
      }
    }
    if (built) {
      ctx->external_storage = file;
      added->push_back("value Environment.getExternalStorageDirectory() = File(/storage/emulated/0)");
    }
    // 外部存储根（getExternal*Dirs 基；Environment 单例同源）
    dexvm::runtime::SetContextExternalRoot("/storage/emulated/0");
    added->push_back("value Context external root = /storage/emulated/0");
  }
}

}  // namespace semface

// 实例字段按名取（超类链回溯；会话 loop 取 Activity.mContentView）。
dexvm::runtime::VObject* SessionFieldRef(dexvm::runtime::VObject* o, const char* name) {
  if (o == nullptr || o->klass == nullptr) return nullptr;
  for (dexvm::runtime::VClass* k = o->klass; k != nullptr; k = k->super_class) {
    for (const auto& f : k->instance_fields) {
      if (f.name == name) {
        dexvm::runtime::ObjHeader* v = nullptr;
        std::memcpy(&v, o->field_data() + f.offset, sizeof(v));
        return reinterpret_cast<dexvm::runtime::VObject*>(v);
      }
    }
  }
  return nullptr;
}

// 生命周期入口 guest 主任务装配（NativeRunApkLifecycle）：main GuestTask 上
// 完成 semface Context 单例装配 + LifecycleDriver 全链（Thread.start/Handler
// 依赖协程上下文；与考卷入口同一引导口径）。keep_alive=true 时首帧后进入
// 常驻会话循环（消息泵/增量 present/点击回灌），直到宿主 nativeSessionStop。
bool RunApkLifecycleGuest(LifecycleGuestRun* run, std::string* error) {
  if (run == nullptr || run->state == nullptr || run->linker == nullptr ||
      run->manifest == nullptr || run->cfg == nullptr || run->driver == nullptr) {
    if (error != nullptr) *error = "invalid lifecycle run args";
    return false;
  }
  const std::string native_lib_dir = run->files_dir + "/nativeLibDir";
  ::mkdir(native_lib_dir.c_str(), 0755);

  // AOSP starting window（WindowManager 首帧前预览层）：launcher Activity 主题
  // 的 android:windowBackground。主题选择按 Android 口径：activity theme 非 0
  // 优先，否则 application theme。资源缝幂等挂载后由 framework 解析（宿主只
  // 按协议显示，不做主题/资源逻辑）。
  {
    dexvm::framework::InstallResourceSeam();
    int32_t sw_theme = run->manifest->application_theme;
    const std::string& launcher = run->manifest->launcher_activity;
    for (const auto& ae : run->manifest->activities) {
      std::string n = ae.name;
      if (!n.empty() && n[0] == '.') n = run->manifest->package_name + n;
      if (n == launcher && ae.theme != 0) {
        sw_theme = ae.theme;
        break;
      }
    }
    StartingWindowInfo info;
    if (dexvm::runtime::ResolveStartingWindowTheme(run->target_apk, sw_theme, &info.color,
                                                   &info.bitmap, &info.gravity)) {
      info.valid = true;
    }
    const bool resolved = info.valid;
    const size_t bitmap_bytes = info.bitmap.size();
    {
      std::lock_guard<std::mutex> lk(g_starting_window_mu);
      g_starting_window = std::move(info);
    }
    ProgressAppend(std::string("starting window: ") + (resolved ? "resolved" : "none") +
                   " theme=0x" + [&] {
                     char b[16];
                     std::snprintf(b, sizeof(b), "%08x", static_cast<uint32_t>(sw_theme));
                     return std::string(b);
                   }() +
                   " bitmap=" + std::to_string(bitmap_bytes) + "B");
  }

  ExamDeviceContext ctx;
  ctx.state = run->state;
  ctx.native_lib_dir = native_lib_dir;
  // APK 内 native 库索引（壳 System.loadLibrary("libDexHelper.so") 真实加载面；
  // 与考卷入口同口径）
  std::string so_err;
  if (!ctx.so_index.Build(run->target_apk, &so_err)) {
    ProgressAppend("apk native libs: index FAILED: " + so_err);
  } else {
    ProgressAppend("apk native libs: index ok");
  }
  // guest JNI 面（壳 native System.loadLibrary -> JNI_OnLoad(VM JNIEnvExt)）
  dexvm::jni::JavaVMExt vm;
  dexvm::jni::InitJavaVMExt(&vm);
  dexvm::jni::JNIEnvExt jni_env;
  dexvm::jni::InitJNIEnvExt(&jni_env, &vm, run->state);
  ctx.vm = &vm;
  ctx.env = &jni_env;
  ctx.app_loader = run->app_loader;
  dexvm::runtime::LibraryLoadHook prev_hook = dexvm::runtime::GetLibraryLoadHook();
  dexvm::runtime::SetLibraryLoadHook(DeviceLibraryLoader);
  ExamDeviceContext* prev_exam = g_exam;
  g_exam = &ctx;
  dexvm::runtime::SetGuestSchedOps(dexvm::runtime::VmSchedBackendOps());
  dexvm::runtime::SetGuestRunBudgetMs(30ll * 60 * 1000);
  bool ok = dexvm::runtime::RunGuestMain(*run->state, [&](dexvm::runtime::VMState& s) -> bool {
    std::vector<std::string> sem_added;
    // 会话数据根：guest Context 文件面 / SharedPreferences 落盘 = 虚拟应用数据
    // 目录（<hostFiles>/apps/<package>；AOSP dataDir 语义）。未显式设置时回落
    // TMPDIR → 设备上常不可写，且跨会话不持久（账户/配置状态每次丢失）。
    dexvm::runtime::SetContextDataDir(run->files_dir + "/" + run->manifest->package_name);
    ProgressAppend("guest data dir: " + run->files_dir + "/" + run->manifest->package_name);
    semface::Complete(s, run->linker, *run->manifest, run->target_apk, run->files_dir,
                      native_lib_dir, &sem_added);
    ProgressAppend("phase frameworkState ok (semface context)");
    // java.io.tmpdir = files 目录（与考卷入口同口径；lib Runtime 依赖可写临时目录）
    dexvm::runtime::VClass* sys_cls = run->linker->FindClass("Ljava/lang/System;");
    if (sys_cls != nullptr) {
      dexvm::runtime::VMethod* setp = nullptr;
      for (auto& m : sys_cls->direct_methods) {
        if (m.name == "setProperty" && m.IsStatic() && m.param_types.size() == 2 &&
            m.param_types[0] == "Ljava/lang/String;" &&
            m.param_types[1] == "Ljava/lang/String;") {
          setp = &m;
          break;
        }
      }
      if (setp != nullptr) {
        dexvm::runtime::Value sa[2] = {};
        sa[0].ref = run->linker->AllocString(u"java.io.tmpdir");
        sa[1].ref = run->linker->AllocString(
            std::u16string(run->files_dir.begin(), run->files_dir.end()));
        dexvm::runtime::Value sr{};
        dexvm::runtime::InvokeMethod(s, setp, sa, 2, &sr);
        s.ClearException();
      }
    }
    run->driver_ok = run->driver->Run(*run->cfg);
    // ---- 常驻会话：首帧后保持 VM 存活，泵消息/增量出画/处理点击 ----
    dexvm::runtime::VObject* activity =
        run->activity_slot != nullptr ? *run->activity_slot : nullptr;
    if (run->keep_alive && run->driver_ok && run->ui != nullptr && activity != nullptr) {
      dexvm::runtime::VObject* content = SessionFieldRef(activity, "mContentView");
      if (content != nullptr) {
        // Dialog 窗口根以会话内容根为逻辑屏幕（宽高为 guest 逻辑窗口）。
        dexvm::runtime::SetDialogHostView(content, run->width, run->height);
        {
          std::lock_guard<std::mutex> lk(g_session_state.mu);
          g_session_state.active = true;
          g_session_state.stop_requested = false;
          g_session_state.pending_click = -1;
          g_session_state.pending_object_click = 0;
          g_session_state.pending_long_object = 0;
          g_session_state.pending_text_object = 0;
          g_session_state.pending_text.clear();
          g_session_state.pending_seek_object = 0;
          g_session_state.pending_seek_progress = 0;
          g_session_state.pending_seek_phase = 0;
          g_session_state.pending_rating_object = 0;
          g_session_state.pending_rating_milli = 0;
          g_session_state.pending_drawer_object = 0;
          g_session_state.pending_drawer_open = 0;
          g_session_state.pending_menu_object = 0;
          g_session_state.pending_menu_index = 0;
          g_session_state.pending_fields_object = 0;
          g_session_state.pending_fields_a = 0;
          g_session_state.pending_fields_b = 0;
          g_session_state.pending_fields_c = 0;
          g_session_state.frames.clear();
          // 首帧全量 envelope 入队（宿主从 full 基线开始物化）
          g_session_state.frames.push_back(run->ui->last_view_tree());
        }
        ProgressAppend("session: live (first frame ready)");
        int64_t session_ticks = 0;
        int64_t session_presented = 0;
        dexvm::bridge::ViewSnapshot last_snap =
            dexvm::bridge::BuildSnapshotFromGuest(s, content);
        const dexvm::runtime::GuestSchedOps* sched = dexvm::runtime::GuestSched();
        while (true) {
          int32_t click = -1;
          uint32_t object_click = 0;
          uint32_t long_object = 0;
          uint32_t text_object = 0;
          std::string text;
          uint32_t seek_object = 0;
          int32_t seek_progress = 0;
          int32_t seek_phase = 0;
          uint32_t rating_object = 0;
          int32_t rating_milli = 0;
          uint32_t drawer_object = 0;
          int32_t drawer_open = 0;
          uint32_t menu_object = 0;
          int32_t menu_index = 0;
          uint32_t fields_object = 0;
          int32_t fields_a = 0;
          int32_t fields_b = 0;
          int32_t fields_c = 0;
          bool stop = false;
          {
            std::lock_guard<std::mutex> lk(g_session_state.mu);
            click = g_session_state.pending_click;
            object_click = g_session_state.pending_object_click;
            long_object = g_session_state.pending_long_object;
            text_object = g_session_state.pending_text_object;
            text.swap(g_session_state.pending_text);
            seek_object = g_session_state.pending_seek_object;
            seek_progress = g_session_state.pending_seek_progress;
            seek_phase = g_session_state.pending_seek_phase;
            rating_object = g_session_state.pending_rating_object;
            rating_milli = g_session_state.pending_rating_milli;
            drawer_object = g_session_state.pending_drawer_object;
            drawer_open = g_session_state.pending_drawer_open;
            menu_object = g_session_state.pending_menu_object;
            menu_index = g_session_state.pending_menu_index;
            fields_object = g_session_state.pending_fields_object;
            fields_a = g_session_state.pending_fields_a;
            fields_b = g_session_state.pending_fields_b;
            fields_c = g_session_state.pending_fields_c;
            g_session_state.pending_click = -1;
            g_session_state.pending_object_click = 0;
            g_session_state.pending_long_object = 0;
            g_session_state.pending_text_object = 0;
            g_session_state.pending_seek_object = 0;
            g_session_state.pending_seek_progress = 0;
            g_session_state.pending_seek_phase = 0;
            g_session_state.pending_rating_object = 0;
            g_session_state.pending_rating_milli = 0;
            g_session_state.pending_drawer_object = 0;
            g_session_state.pending_drawer_open = 0;
            g_session_state.pending_menu_object = 0;
            g_session_state.pending_menu_index = 0;
            g_session_state.pending_fields_object = 0;
            g_session_state.pending_fields_a = 0;
            g_session_state.pending_fields_b = 0;
            g_session_state.pending_fields_c = 0;
            stop = g_session_state.stop_requested;
          }
          if (object_click != 0) {
            std::string click_status = dexvm::bridge::InjectGuestObjectClick(&s, object_click);
            ProgressAppend("session click objectId=" + std::to_string(object_click) + ": " +
                           click_status);
          } else if (click >= 0) {
            std::string click_status = dexvm::bridge::InjectGuestClick(&s, click);
            ProgressAppend("session click " + std::to_string(click) + ": " + click_status);
          }
          if (long_object != 0) {
            std::string status = dexvm::bridge::InjectGuestObjectLongClick(&s, long_object);
            ProgressAppend("session long-click objectId=" + std::to_string(long_object) + ": " +
                           status);
          }
          if (text_object != 0) {
            std::string status = dexvm::bridge::InjectGuestObjectText(&s, text_object, text);
            ProgressAppend("session text objectId=" + std::to_string(text_object) + ": " +
                           status);
          }
          if (seek_object != 0) {
            std::string status = dexvm::bridge::InjectGuestObjectSeek(
                &s, seek_object, seek_progress, seek_phase);
            ProgressAppend("session seek objectId=" + std::to_string(seek_object) + ": " +
                           status);
          }
          if (rating_object != 0) {
            std::string status =
                dexvm::bridge::InjectGuestObjectRating(&s, rating_object, rating_milli);
            ProgressAppend("session rating objectId=" + std::to_string(rating_object) + ": " +
                           status);
          }
          if (drawer_object != 0) {
            std::string status = dexvm::bridge::InjectGuestObjectDrawer(
                &s, drawer_object, drawer_open != 0);
            ProgressAppend("session drawer objectId=" + std::to_string(drawer_object) + ": " +
                           status);
          }
          if (menu_object != 0) {
            std::string status =
                dexvm::bridge::InjectGuestObjectMenuClick(&s, menu_object, menu_index);
            ProgressAppend("session menu objectId=" + std::to_string(menu_object) + ": " +
                           status);
          }
          if (fields_object != 0) {
            std::string status = dexvm::bridge::InjectGuestObjectFields(
                &s, fields_object, fields_a, fields_b, fields_c);
            ProgressAppend("session fields objectId=" + std::to_string(fields_object) + ": " +
                           status);
          }
          if (stop) break;
          // 设备验证探针（marker 文件）：走真实 Dialog.show/dismiss 全链路
          // （lib Dialog/Window -> VmLayoutHost intrinsic -> 快照叠加根 ->
          // 宿主物化）。仅诊断用，marker 不存在时零行为。
          {
            const std::string appcompat_marker =
                run->files_dir + "/vm_dialog_probe_appcompat";
            if (session_ticks >= 1 && ::access(appcompat_marker.c_str(), F_OK) == 0) {
              ::unlink(appcompat_marker.c_str());
              if (dexvm::runtime::DebugDialogProbeShowAppCompat(s, activity)) {
                ProgressAppend("dialog probe: appcompat show");
              } else {
                ProgressAppend("dialog probe: appcompat show FAILED");
              }
            }
            const std::string show_marker = run->files_dir + "/vm_dialog_probe";
            if (session_ticks >= 1 && ::access(show_marker.c_str(), F_OK) == 0) {
              ::unlink(show_marker.c_str());
              if (dexvm::runtime::DebugDialogProbeShow(s, activity)) {
                ProgressAppend("dialog probe: show");
              } else {
                ProgressAppend("dialog probe: show FAILED");
              }
            }
            const std::string dismiss_marker = run->files_dir + "/vm_dialog_probe_dismiss";
            if (::access(dismiss_marker.c_str(), F_OK) == 0) {
              ::unlink(dismiss_marker.c_str());
              dexvm::runtime::DebugDialogProbeDismiss(s);
              ProgressAppend("dialog probe: dismiss");
            }
          }
          // 主线程消息泵（Handler post 派发；guest 线程任务经调度等待点运行）
          session_ticks++;
          auto pump_t0 = std::chrono::steady_clock::now();
          std::string pump_err;
          size_t pumped = 0;
          if (!dexvm::runtime::PumpLooperQueuesRealtime(s, &pump_err, &pumped) &&
              !pump_err.empty()) {
            ProgressAppend("session pump error: " + pump_err);
          }
          auto pump_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - pump_t0)
                             .count();
          if (session_ticks % 40 == 0) {
            ProgressAppend("session tick " + std::to_string(session_ticks) + " pump=" +
                           std::to_string(pump_ms) + "ms msgs=" + std::to_string(pumped) +
                           " idle=" + std::to_string(dexvm::runtime::IdleHandlerRuns()) +
                           " presented=" + std::to_string(session_presented));
          }
          // AOSP Choreographer doFrame 口径：requestLayout/invalidate 置脏
          // 或视图树变化 → 本帧 traversal（measure/layout/draw + observer
          // 派发）。异步 UI（ViewPager/RecyclerView adapter 绑定、延迟
          // addView）全靠这个循环产生子视图。
          bool layout_dirty = dexvm::runtime::TakeGuestLayoutRequest();
          dexvm::bridge::ViewSnapshot probe =
              dexvm::bridge::BuildSnapshotFromGuest(s, content);
          bool tree_changed = !dexvm::bridge::DiffSnapshots(last_snap, probe).ops.empty();
          if (layout_dirty || tree_changed) {
            dexvm::framework::PerformTraversals(s, content, run->width, run->height);
            dexvm::runtime::DispatchViewTreeGlobalLayout(s, content);
            dexvm::runtime::DispatchViewTreePreDraw(s, content);
            dexvm::framework::PerformTraversals(s, content, run->width, run->height);
          }
          // 视图树变化 -> present（mirror 出 patch）并入队
          dexvm::bridge::ViewSnapshot snap =
              dexvm::bridge::BuildSnapshotFromGuest(s, content);
          dexvm::bridge::ViewPatch patch = dexvm::bridge::DiffSnapshots(last_snap, snap);
          if (!patch.ops.empty()) {
            if (dexvm::bridge::ViewTreeExporterFn exporter =
                    dexvm::bridge::GetViewTreeExporter()) {
              exporter(&s, content, run->width, run->height, run->ui);
              last_snap = std::move(snap);
              bool overflow = false;
              {
                std::lock_guard<std::mutex> lk(g_session_state.mu);
                overflow = g_session_state.frames.size() > 256;
                if (overflow) g_session_state.frames.clear();
              }
              if (overflow) {
                // 队列防线：宿主滞后太多 -> 重置 mirror 并出全量快照重基
                dexvm::bridge::ResetUiBridgeProtocol();
                if (dexvm::bridge::ViewTreeExporterFn exp2 =
                        dexvm::bridge::GetViewTreeExporter()) {
                  exp2(&s, content, run->width, run->height, run->ui);
                  last_snap = dexvm::bridge::BuildSnapshotFromGuest(s, content);
                }
              }
              std::vector<uint8_t> env = run->ui->last_view_tree();
              session_presented++;
              std::lock_guard<std::mutex> lk(g_session_state.mu);
              g_session_state.frames.push_back(std::move(env));
            }
          }
          // 50ms 协作等待（stop/点击可提前唤醒；等待点让出给 guest 线程）
          if (sched != nullptr && sched->wait != nullptr && sched->now_ms != nullptr) {
            sched->wait(0, sched->now_ms() + 50, SessionWakePred, &g_session_state);
          } else {
            ::usleep(50 * 1000);
          }
        }
        {
          std::lock_guard<std::mutex> lk(g_session_state.mu);
          g_session_state.active = false;
          g_session_state.frames.clear();
        }
        ProgressAppend("session: stopped");
      }
    }
    // 收卷：生命周期链完成即停调度器（残留 guest 线程不拖住 RunGuestMain）
    if (const dexvm::runtime::GuestSchedOps* sched = dexvm::runtime::GuestSched()) {
      sched->request_stop();
    }
    return true;
  });
  dexvm::runtime::SetLibraryLoadHook(prev_hook);
  // 会话/生命周期收尾：清理 guest 线程上下文表（g_ctx_by_task 等跨 VM 残留
  // 会让下一次 RunGuestMain 的新 task 句柄命中旧堆 Thread peer —— dirty
  // intrinsic outside-heap。考卷入口同口径）。
  dexvm::runtime::GuestRuntimeReset(run->state);
  g_exam = prev_exam;
  if (!ok) {
    if (error != nullptr) *error = dexvm::runtime::GuestRunFailureReason();
    return false;
  }
  return true;
}

size_t CountRegisteredNatives(dexvm::runtime::ClassLinker* linker) {
  size_t n = 0;
  if (linker == nullptr) return 0;
  for (dexvm::runtime::VClass* k : linker->all_classes()) {
    for (const auto& m : k->direct_methods) {
      if (m.jni_native_fn != nullptr) n++;
    }
    for (const auto& m : k->virtual_methods) {
      if (m.jni_native_fn != nullptr) n++;
    }
  }
  return n;
}

// dlerror 依赖缺口："library \"libX.so\" not found" -> libX.so
std::string ParseMissingDep(const std::string& err) {
  size_t p = err.rfind("library \"");
  if (p == std::string::npos) return "";
  p += std::strlen("library \"");
  size_t q = err.find('"', p);
  if (q == std::string::npos) return "";
  return err.substr(p, q - p);
}

// 递归预载 APK native 依赖：从 APK lib/<abi>/ 提取到 nativeLibDir 并
// dlopen(RTLD_GLOBAL)，让随后 dlopen 的宿主 so 的 DT_NEEDED 在同命名空间
// 按 soname 命中。RN 库链很深（libfabricjni → libreact_render_animations →
// libreact_render_graphics → ...），只处理第一层会在深层缺失时直接失败
// （实证：RN init UnsatisfiedLinkError: libreact_render_animations.so）。
bool PreloadApkNativeDep(ExamDeviceContext* ctx, const std::string& name, int depth,
                         std::string* error) {
  if (ctx == nullptr) {
    *error = "exam device loader not armed";
    return false;
  }
  if (depth > 32) {
    *error = "dep preload depth exceeded at " + name;
    return false;
  }
  const std::string path = ctx->native_lib_dir + "/" + name;
  if (!FileExists(path)) {
    const ApkSoEntry* e = ctx->so_index.Find(name);
    if (e == nullptr) {
      *error = "dep " + name + " not in APK native libs";
      return false;
    }
    std::string xerr;
    if (!ctx->so_index.ExtractTo(*e, path, &xerr)) {
      *error = "extract dep " + name + " failed: " + xerr;
      return false;
    }
  }
  // 本层可能还缺更深的 DT_NEEDED：解析后递归预载，再重试本层（链接器
  // 按 soname 复用已 RTLD_GLOBAL 载入的同命名空间库）。
  for (int retry = 0; retry < 16; retry++) {
    void* h = ::dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (h != nullptr) return true;
    const char* de = ::dlerror();
    const std::string detail = de != nullptr ? de : path;
    const std::string next = ParseMissingDep(detail);
    if (next.empty() || next == name) {
      *error = "preload dep " + name + " failed: " + detail;
      return false;
    }
    if (!PreloadApkNativeDep(ctx, next, depth + 1, error)) return false;
  }
  *error = "preload dep " + name + " failed after retries";
  return false;
}

// System.load(路径) 缺失时按 basename 回落 nativeLibDir（安装布局等价语义：
// guest 全路径指向的 nativeLibraryDir 即本 VM 的 files/nativeLibDir）。
std::string ResolveSoPath(ExamDeviceContext* ctx, const std::string& name, LibLoadRecord* rec,
                          std::string* error) {
  if (name.find('/') != std::string::npos && FileExists(name)) {
    rec->extract = "resolved as given path";
    return name;
  }
  const std::string base = BaseName(name);
  std::string path = ctx->native_lib_dir + "/" + base;
  if (FileExists(path)) {
    rec->extract = name.find('/') != std::string::npos
                       ? "basename fallback from " + name + " (already in nativeLibDir)"
                       : "already in nativeLibDir";
    return path;
  }
  const ApkSoEntry* e = ctx->so_index.Find(base);
  if (e == nullptr) {
    *error = "couldn't find " + name + " (no " + std::string(kAbiDir) + base +
             " in APK; not in nativeLibDir)";
    return "";
  }
  std::string xerr;
  if (!ctx->so_index.ExtractTo(*e, path, &xerr)) {
    *error = "extract " + e->name + " -> " + path + " failed: " + xerr;
    return "";
  }
  rec->extract = "extracted " + std::to_string(e->uncomp_size) + " bytes from " + e->name +
                 (name.find('/') != std::string::npos ? " (basename fallback from " + name + ")"
                                                      : "");
  return path;
}

// guest 异常归因串（descriptor: detailMessage，cause 链 ≤4 级；loadLibrary 段
// <clinit> 失败定位用——erroneous 收口后归因要落到底层 cause）。
std::string DescribeGuestException(dexvm::runtime::ObjHeader* ex) {
  std::string out;
  dexvm::runtime::ObjHeader* cur = ex;
  for (int depth = 0; cur != nullptr && cur->klass != nullptr && depth < 4; depth++) {
    if (depth > 0) out += " <- ";
    out += cur->klass->descriptor;
    dexvm::runtime::VObject* eo = reinterpret_cast<dexvm::runtime::VObject*>(cur);
    dexvm::runtime::ObjHeader* cause = nullptr;
    for (dexvm::runtime::VClass* kk = eo->klass; kk != nullptr; kk = kk->super_class) {
      for (const auto& f : kk->instance_fields) {
        if (f.name == "detailMessage") {
          dexvm::runtime::ObjHeader* s = nullptr;
          std::memcpy(&s, eo->field_data() + f.offset, sizeof(s));
          if (s != nullptr) {
            out += ": " + static_cast<dexvm::runtime::VString*>(s)->ToModifiedUtf8();
          }
        } else if (f.name == "cause" && cause == nullptr) {
          std::memcpy(&cause, eo->field_data() + f.offset, sizeof(cause));
        }
      }
    }
    if (cause == cur) cause = nullptr;  // cause 默认自指（未 initCause）
    if (cause == nullptr) {
      // InvocationTargetException.target（反射构造器/方法包装的根因）
      for (dexvm::runtime::VClass* kk = eo->klass; kk != nullptr; kk = kk->super_class) {
        for (const auto& f : kk->instance_fields) {
          if (f.name == "target") {
            std::memcpy(&cause, eo->field_data() + f.offset, sizeof(cause));
            break;
          }
        }
        if (cause != nullptr) break;
      }
    }
    cur = cause;
  }
  return out;
}

// LibraryLoadHook 实装（docs 07 章 16.2 设备端）：真 loader，不伪造。
bool DeviceLibraryLoader(const std::string& name, std::string* error) {
  ExamDeviceContext* ctx = g_exam;
  LibLoadRecord rec;
  rec.name = name;
  if (ctx == nullptr || ctx->vm == nullptr || ctx->env == nullptr) {
    *error = "exam device loader not armed";
    return false;
  }
  std::string path = ResolveSoPath(ctx, name, &rec, error);
  if (path.empty()) {
    rec.detail = *error;
    rec.ok = false;
    ctx->log.push_back(std::move(rec));
    return false;
  }
  rec.path = path;

  // dlopen + JNI_OnLoad（VM JNIEnvExt 面；RegisterNatives 经 JNI 函数表生效）。
  // 依赖缺口重试：DT_NEEDED 未命中系统库而在 APK native 目录时，预载
  // RTLD_GLOBAL（链接器同名复用，等价 classloader namespace 的 libPath 解析）。
  // CrashGuard：guest native 真实执行可崩（如壳对 NULL 返回不判空）——记录为
  // 缺口、保住报告，不伪造成功。
  CrashGuard guard;
  for (int attempt = 0;; attempt++) {
    std::string load_err;
    bool loaded = guard.Run(
        [&](std::string* e) {
          return ctx->vm->natives.LoadNativeLibrary(ctx->env, path, ctx->app_loader, nullptr, e);
        },
        &load_err);
    if (loaded) {
      const dexvm::jni::SharedLibrary* lib = ctx->vm->natives.Find(path);
      rec.ok = true;
      if (lib != nullptr && lib->on_load_called && lib->on_load_version != 0) {
        rec.detail = "dlopen ok, JNI_OnLoad version=" + std::to_string(lib->on_load_version) + " ok";
      } else {
        rec.detail = "dlopen ok, no JNI_OnLoad";
      }
      rec.detail += ", registered natives now " +
                    std::to_string(CountRegisteredNatives(ctx->state->linker));
      // JNI_OnLoad 期间 RegisterNatives/FindClass 抛出的 guest 异常按
      // loadLibrary 语义上抛（builtins LoadLibraryCommon 传播 pending）
      if (ctx->state->HasPendingException()) {
        dexvm::runtime::ObjHeader* ex = ctx->state->pending_exception;
        std::string desc = DescribeGuestException(ex);
        if (ex == nullptr) desc = "Ljava/lang/Throwable;";
        for (size_t bi = 0; bi < ctx->state->last_throw_backtrace.size() && bi < 4; bi++) {
          desc += "\n        at " + ctx->state->last_throw_backtrace[bi];
        }
        rec.ok = false;
        rec.detail += "; guest exception pending after load: " + desc;
        *error = rec.detail;
        ctx->log.push_back(std::move(rec));
        return false;
      }
      ctx->log.push_back(std::move(rec));
      return true;
    }
    if (load_err.rfind("guest native crash", 0) == 0) {
      // guest native 崩溃（真实执行）：不重试，按缺口记录
      *error = load_err + " during dlopen/JNI_OnLoad of " + path;
      rec.detail = *error;
      rec.ok = false;
      ctx->log.push_back(std::move(rec));
      return false;
    }
    std::string dep = ParseMissingDep(load_err);
    if (attempt >= 4 || dep.empty() || dep == BaseName(path)) {
      *error = load_err;
      rec.detail = attempt > 0 ? load_err + " (after " + std::to_string(attempt) + " dep preloads)"
                               : load_err;
      rec.ok = false;
      ctx->log.push_back(std::move(rec));
      return false;
    }
    // 依赖在 APK native 目录：递归提取 + 预载（依赖不经 JNI_OnLoad——链接器语义；
    // 传递依赖同样递归处理，见 PreloadApkNativeDep）。
    std::string dep_err;
    if (!PreloadApkNativeDep(ctx, dep, 0, &dep_err)) {
      *error = load_err + " (" + dep_err + ")";
      rec.detail = *error;
      rec.ok = false;
      ctx->log.push_back(std::move(rec));
      return false;
    }
    rec.detail += "preloaded dep " + dep + "; ";
  }
}

// ---- 缺口分类（exam_smoke 同款；kNativeLib 建议修法按设备端措辞）----

enum class GapType {
  kMissingClass,
  kMissingMethod,
  kMissingMethodBody,
  kMissingField,
  kUnsupported,
  kException,
  kNativeLib,
  kOther,
};

const char* GapTypeName(GapType t) {
  switch (t) {
    case GapType::kMissingClass: return "missing-class";
    case GapType::kMissingMethod: return "missing-method";
    case GapType::kMissingMethodBody: return "missing-method-body";
    case GapType::kMissingField: return "missing-field";
    case GapType::kUnsupported: return "unsupported";
    case GapType::kException: return "exception";
    case GapType::kNativeLib: return "native-lib";
    default: return "other";
  }
}

struct Gap {
  GapType type = GapType::kOther;
  std::string target;   // guest 类.方法/字段（descriptor/签名文本）
  std::string phase;    // 生命周期 phase
  std::string caller;   // 调用方（guest 栈顶或 phase）
  std::vector<std::string> stack;  // guest 栈快照（抛出瞬间）
  std::string diagnosis;  // 异常类:消息 / 原始错误
  std::string fix;      // 建议修法（语义归因，非壳适配）
};

// jre-excluded 黑名单（runtime-libs/jre-excluded.txt：VM 内建职责）
bool IsJreExcluded(const std::string& desc) {
  static const char* kList[] = {
      "Ljava/lang/Object;",       "Ljava/lang/Class;",
      "Ljava/lang/ClassLoader;",  "Ljava/lang/Thread;",
      "Ljava/lang/ThreadGroup;",  "Ljava/lang/Throwable;",
      "Ljava/lang/System;",       "Ljava/lang/Runtime;",
      "Ljava/lang/Enum;",         "Ljava/lang/reflect/Method;",
      "Ljava/lang/reflect/Constructor;", "Ljava/lang/reflect/Proxy;",
      "Ljava/lang/reflect/InvocationHandler;",
      "Ljava/util/concurrent/locks/LockSupport;",
      "Ljava/io/FileDescriptor;", "Ljava/io/InputStream;",
      "Ljava/io/FileInputStream;", "Ljava/io/FileOutputStream;",
      "Ljava/net/Socket;",        "Ljava/net/ServerSocket;",
      "Ljava/lang/Process;",      "Ljava/lang/ProcessBuilder;",
      // Lsun/misc/Unsafe; 已按 §31.7 归位 lib 壳（runtime.dex 打包，转发
      // jdk.internal.misc.Unsafe）；VM 内建职责只剩 jdk 面。
      "Ljdk/internal/misc/Unsafe;",
  };
  for (const char* d : kList) {
    if (desc == d) return true;
  }
  return false;
}

// 建议修法：按缺口类型 + 目标 descriptor 家族归因（语义面，非壳特判）
std::string SuggestFix(GapType type, const std::string& target) {
  // 目标可能是类 descriptor、"Lcls;.name(sig)ret" 或 "Lcls;.name:type"
  std::string cls = target;
  size_t dot = cls.find('.');
  if (dot != std::string::npos) cls = cls.substr(0, dot);
  bool javaish = cls.rfind("Ljava/", 0) == 0 || cls.rfind("Lsun/", 0) == 0 ||
                 cls.rfind("Ljdk/", 0) == 0;
  bool androidish = cls.rfind("Landroid/", 0) == 0 || cls.rfind("Landroidx/", 0) == 0;
  bool dalvik = cls.rfind("Ldalvik/", 0) == 0;

  switch (type) {
    case GapType::kMissingClass:
      if (IsJreExcluded(cls)) {
        return "VM 内建类（jre-excluded 黑名单，core/builtins 注册）";
      }
      if (javaish) return "VM 内建类（java 常用面，core/builtins 注册）";
      if (dalvik) return "VM 内建（dalvik.system 语义面，core/builtins）";
      if (androidish) return "framework 内建类补全（core/framework_builtins）";
      return "app dex 类缺失（loader 覆盖范围/类不存在——归因后置）";
    case GapType::kMissingMethod:
    case GapType::kMissingMethodBody:
      if (javaish || dalvik) return "VM intrinsic 方法面补全（core/builtins）";
      if (androidish) return "framework 内建方法面补全（core/framework_builtins）";
      return "guest 方法面缺口（接口/父类语义或解释器派发语义）";
    case GapType::kMissingField:
      if (javaish || dalvik) return "VM 内建字段面补全（core/builtins）";
      if (androidish) return "framework 内建字段面补全（core/framework_builtins）";
      return "guest 字段面缺口（类布局/链接语义）";
    case GapType::kUnsupported:
      return "解释器/内建语义缺口（core/interpreter 或 builtins 按 Android 语义补）";
    case GapType::kNativeLib:
      return "loadLibrary 语义面/库依赖/权限边界（设备 dlopen 实测见 native 库执行结果；"
             "非设备环境时为宿主特权边界 docs 29.2）";
    case GapType::kException:
      return "异常路径按 Android 语义补齐（core/exception + builtins 异常类）";
    default:
      return "按卡点诊断归因（语义缺口或宿主特权边界）";
  }
}

// 从 phase 失败诊断 + guest 栈快照提取缺口
std::vector<Gap> ClassifyFailure(const std::string& phase, const std::string& error,
                                 const std::vector<std::string>& stack) {
  std::vector<Gap> out;
  Gap g;
  g.phase = phase;
  g.diagnosis = error;
  g.stack = stack;
  g.caller = stack.empty() ? "(host) phase=" + phase : stack[0] + " (phase=" + phase + ")";

  auto trim = [](std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == ')')) s.pop_back();
    return s;
  };

  // 1. "class not found: <desc>"
  size_t p = error.find("class not found: ");
  if (p != std::string::npos) {
    std::string desc = error.substr(p + std::strlen("class not found: "));
    size_t cut = desc.find(" (");
    if (cut != std::string::npos) desc = desc.substr(0, cut);
    g.type = GapType::kMissingClass;
    g.target = trim(desc);
    g.fix = SuggestFix(g.type, g.target);
    out.push_back(g);
    return out;
  }
  // 2. "no such method on <cls>"
  p = error.find("no such method on ");
  if (p != std::string::npos) {
    std::string cls = error.substr(p + std::strlen("no such method on "));
    size_t cut = cls.find(" (");
    if (cut != std::string::npos) cls = cls.substr(0, cut);
    g.type = GapType::kMissingMethod;
    g.target = trim(cls);
    g.fix = SuggestFix(g.type, g.target);
    out.push_back(g);
    return out;
  }
  // 3. 异常诊断 "<desc>;: <msg>"（desc 形如 Ljava/lang/XxxError;，可被外层
  //    "(...)" 包裹——如 class init failed 包装）
  {
    size_t semi = error.rfind(";: ");
    if (semi != std::string::npos) {
      // 向前回溯 descriptor（'L' + [A-Za-z0-9_/$]* + ';'；'L' 是 alnum 会被
      // 吞进回溯，故落点即 'L' 起点，校验前一字符为分隔符）
      size_t start = semi;
      while (start > 0) {
        char c = error[start - 1];
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '/') {
          start--;
        } else {
          break;
        }
      }
      bool boundary_ok =
          start < semi && error[start] == 'L' &&
          (start == 0 || !(isalnum(static_cast<unsigned char>(error[start - 1])) ||
                           error[start - 1] == '_' || error[start - 1] == '$' ||
                           error[start - 1] == '/'));
      if (boundary_ok) {
        std::string desc = error.substr(start, semi + 1 - start);
        std::string msg = error.substr(semi + 3);
        while (!msg.empty() && (msg.back() == ')' || msg.back() == ' ')) msg.pop_back();
        g.diagnosis = desc + ": " + msg;
        if (desc == "Ljava/lang/NoClassDefFoundError;") {
          g.type = GapType::kMissingClass;
          g.target = trim(msg);
        } else if (desc == "Ljava/lang/NoSuchMethodError;") {
          g.type = GapType::kMissingMethod;
          g.target = trim(msg);
        } else if (desc == "Ljava/lang/NoSuchFieldError;") {
          g.type = GapType::kMissingField;
          g.target = trim(msg);
        } else if (desc == "Ljava/lang/UnsupportedOperationException;" &&
                   msg.rfind("no code: ", 0) == 0) {
          g.type = GapType::kMissingMethodBody;
          g.target = trim(msg.substr(std::strlen("no code: ")));
        } else if (desc == "Ljava/lang/UnsupportedOperationException;") {
          g.type = GapType::kUnsupported;
          g.target = trim(msg);
        } else if (desc == "Ljava/lang/UnsatisfiedLinkError;") {
          g.type = GapType::kNativeLib;
          g.target = trim(msg);
        } else if (desc == "Ljava/lang/AbstractMethodError;") {
          g.type = GapType::kMissingMethodBody;
          g.target = trim(msg);
        } else {
          g.type = GapType::kException;
          g.target = desc + (msg.empty() ? "" : ": " + trim(msg));
        }
        g.fix = SuggestFix(g.type, g.target);
        out.push_back(g);
        return out;
      }
    }
  }
  // 4. 其它（含 "no code: <sig>" 裸消息）
  p = error.find("no code: ");
  if (p != std::string::npos) {
    g.type = GapType::kMissingMethodBody;
    g.target = trim(error.substr(p + std::strlen("no code: ")));
    g.fix = SuggestFix(g.type, g.target);
    out.push_back(g);
    return out;
  }
  g.type = GapType::kOther;
  g.target = error;
  g.fix = SuggestFix(g.type, g.target);
  out.push_back(g);
  return out;
}

// ---- 语义面自检（新内建面经真实解释器执行验证，报告记录）----
struct Probe {
  std::string name;
  bool ok = false;
  std::string detail;
};

dexvm::runtime::VMethod* ProbeFindStatic(dexvm::runtime::VClass* klass, const char* name,
                                         std::vector<std::string> params, const char* ret) {
  for (auto& m : klass->direct_methods) {
    if (m.IsStatic() && m.name == name && m.return_type == ret && m.param_types == params) return &m;
  }
  return nullptr;
}
dexvm::runtime::VMethod* ProbeFindVirtual(dexvm::runtime::VClass* klass, const char* name,
                                          std::vector<std::string> params, const char* ret) {
  dexvm::runtime::VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  return klass->FindInterfaceImpl(&probe);
}

void RunFaceProbes(dexvm::runtime::VMState& state, dexvm::runtime::ClassLinker& linker,
                   std::vector<Probe>* out) {
  using dexvm::runtime::InvokeMethod;
  using dexvm::runtime::ObjHeader;
  using dexvm::runtime::Value;
  using dexvm::runtime::VArray;
  using dexvm::runtime::VClass;
  using dexvm::runtime::VMethod;
  using dexvm::runtime::VObject;
  using dexvm::runtime::VString;

  auto add = [&](const char* name, bool ok, const std::string& detail) {
    Probe p;
    p.name = name;
    p.ok = ok;
    p.detail = detail;
    out->push_back(std::move(p));
    state.ClearException();
  };

  // 1. 链接补全接口：java.util.ArrayList 可链接+初始化（Iterable/Cloneable 面）
  {
    VClass* al = linker.FindClass("Ljava/util/ArrayList;");
    bool ok = al != nullptr && linker.InitializeClass(al, &state) && !state.HasPendingException();
    std::string detail = al == nullptr ? linker.last_error() : "linked+initialized";
    if (!ok && state.HasPendingException()) detail += " (pending)";
    add("link java.util.ArrayList", ok, detail);
  }
  // 2. System 常用静态面：currentTimeMillis
  {
    VClass* sys = linker.FindClass("Ljava/lang/System;");
    VMethod* m = sys != nullptr ? ProbeFindStatic(sys, "currentTimeMillis", {}, "J") : nullptr;
    Value r{};
    bool ok = m != nullptr && InvokeMethod(state, m, nullptr, 0, &r) && r.l != 0;
    add("System.currentTimeMillis()", ok, ok ? std::to_string(r.l) : "invoke failed");
  }
  // 3. 反射面：Class.getDeclaredMethod + Method.invoke（docs 29 壳必用）
  {
    VClass* cls_class = linker.FindClass("Ljava/lang/Class;");
    VClass* sb = linker.FindClass("Ljava/lang/StringBuilder;");
    VClass* cls_arr = linker.FindArrayClass(cls_class);
    bool ok = false;
    std::string detail = "skipped";
    if (cls_class != nullptr && sb != nullptr) {
      // StringBuilder.append(String) 反射调用
      VArray* params = linker.AllocArray(cls_arr, 1);
      if (params != nullptr) {
        ObjHeader* str_cls = reinterpret_cast<ObjHeader*>(linker.FindClass("Ljava/lang/String;"));
        std::memcpy(params->data(), &str_cls, sizeof(str_cls));
      }
      VString* name = linker.AllocString(u"append");
      VMethod* gdm = ProbeFindVirtual(cls_class, "getDeclaredMethod",
                                      {"Ljava/lang/String;", "[Ljava/lang/Class;"},
                                      "Ljava/lang/reflect/Method;");
      Value args[3] = {};
      args[0].ref = reinterpret_cast<ObjHeader*>(sb);
      args[1].ref = reinterpret_cast<ObjHeader*>(name);
      args[2].ref = reinterpret_cast<ObjHeader*>(params);
      Value mirror{};
      if (gdm != nullptr && InvokeMethod(state, gdm, args, 3, &mirror) && mirror.ref != nullptr) {
        // setAccessible(true) + invoke(sbInstance, ["hi"])
        VMethod* sa = ProbeFindVirtual(mirror.ref->klass, "setAccessible", {"Z"}, "V");
        Value sa_a[2] = {};
        sa_a[0].ref = mirror.ref;
        sa_a[1].i = 1;
        if (sa != nullptr) InvokeMethod(state, sa, sa_a, 2, nullptr);
        VObject* sbi = linker.AllocObject(sb);
        VMethod* ctor = ProbeFindVirtual(sb, "<init>", {}, "V");
        Value cself{};
        cself.ref = sbi;
        if (ctor != nullptr) InvokeMethod(state, ctor, &cself, 1, nullptr);
        VClass* obj_arr = linker.FindArrayClass(linker.FindClass("Ljava/lang/Object;"));
        VArray* argv = linker.AllocArray(obj_arr, 1);
        ObjHeader* hi = reinterpret_cast<ObjHeader*>(linker.AllocString(u"hi"));
        if (argv != nullptr) std::memcpy(argv->data(), &hi, sizeof(hi));
        VMethod* inv = ProbeFindVirtual(mirror.ref->klass, "invoke",
                                        {"Ljava/lang/Object;", "[Ljava/lang/Object;"},
                                        "Ljava/lang/Object;");
        Value ia[3] = {};
        ia[0].ref = mirror.ref;
        ia[1].ref = reinterpret_cast<ObjHeader*>(sbi);
        ia[2].ref = reinterpret_cast<ObjHeader*>(argv);
        Value ret{};
        if (inv != nullptr && InvokeMethod(state, inv, ia, 3, &ret) && ret.ref != nullptr) {
          // append 返回 this；toString 应为 "hi"
          VMethod* ts = ProbeFindVirtual(ret.ref->klass, "toString", {}, "Ljava/lang/String;");
          Value tself{};
          tself.ref = ret.ref;
          Value outv{};
          if (ts != nullptr && InvokeMethod(state, ts, &tself, 1, &outv) && outv.ref != nullptr) {
            std::string got = static_cast<VString*>(outv.ref)->ToModifiedUtf8();
            ok = got == "hi";
            detail = ok ? "append(String) via reflection -> \"hi\"" : "got \"" + got + "\"";
          }
        }
        if (!ok && detail == "skipped") detail = "invoke chain failed";
      }
    }
    add("Class.getDeclaredMethod + Method.invoke", ok, detail);
  }
  // 4. hidden API 面：VMRuntime.setHiddenApiExemptions（docs 29.2③）
  {
    VClass* vmr = linker.FindClass("Ldalvik/system/VMRuntime;");
    VMethod* gr = vmr != nullptr ? ProbeFindStatic(vmr, "getRuntime", {}, "Ldalvik/system/VMRuntime;")
                                : nullptr;
    Value rt{};
    bool ok = gr != nullptr && InvokeMethod(state, gr, nullptr, 0, &rt) && rt.ref != nullptr;
    std::string detail = "getRuntime failed";
    if (ok) {
      VClass* str_cls2 = linker.FindClass("Ljava/lang/String;");
      VClass* str_arr = linker.FindArrayClass(str_cls2);
      VArray* ex = linker.AllocArray(str_arr, 1);
      ObjHeader* L = reinterpret_cast<ObjHeader*>(linker.AllocString(u"L"));
      if (ex != nullptr) std::memcpy(ex->data(), &L, sizeof(L));
      VMethod* se = ProbeFindVirtual(rt.ref->klass, "setHiddenApiExemptions",
                                     {"[Ljava/lang/String;"}, "V");
      Value sa[2] = {};
      sa[0].ref = rt.ref;
      sa[1].ref = reinterpret_cast<ObjHeader*>(ex);
      ok = se != nullptr && InvokeMethod(state, se, sa, 2, nullptr);
      detail = ok ? "setHiddenApiExemptions([L]) recorded" : "setHiddenApiExemptions failed";
    }
    add("VMRuntime.setHiddenApiExemptions", ok, detail);
  }
  // 5. Thread.currentThread 语义面
  {
    VClass* thr = linker.FindClass("Ljava/lang/Thread;");
    VMethod* ct = thr != nullptr ? ProbeFindStatic(thr, "currentThread", {}, "Ljava/lang/Thread;")
                                : nullptr;
    Value r{};
    bool ok = ct != nullptr && InvokeMethod(state, ct, nullptr, 0, &r) && r.ref != nullptr;
    std::string detail = ok ? "current thread allocated" : "currentThread failed";
    if (ok) {
      VMethod* gn = ProbeFindVirtual(r.ref->klass, "getName", {}, "Ljava/lang/String;");
      Value self{};
      self.ref = r.ref;
      Value nm{};
      if (gn != nullptr && InvokeMethod(state, gn, &self, 1, &nm) && nm.ref != nullptr) {
        detail += " name=" + static_cast<VString*>(nm.ref)->ToModifiedUtf8();
      }
    }
    add("Thread.currentThread()", ok, detail);
  }
  // 6. 异常类补全面
  {
    const char* kNeed[] = {"Ljava/lang/UnsatisfiedLinkError;", "Ljava/lang/IllegalAccessError;",
                           "Ljava/lang/NoSuchMethodException;", "Ljava/lang/SecurityException;",
                           "Ljava/lang/reflect/InvocationTargetException;"};
    bool ok = true;
    std::string missing;
    for (const char* d : kNeed) {
      if (linker.FindClass(d) == nullptr) {
        ok = false;
        missing += d;
      }
    }
    add("exception class batch", ok, ok ? "all present" : "missing " + missing);
  }
}

// printf 风格报告拼装
void AppendFmt(std::string* out, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void AppendFmt(std::string* out, const char* fmt, ...) {
  char stack_buf[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = std::vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (static_cast<size_t>(n) < sizeof(stack_buf)) {
    out->append(stack_buf, static_cast<size_t>(n));
    return;
  }
  std::vector<char> big(static_cast<size_t>(n) + 1);
  va_start(ap, fmt);
  std::vsnprintf(big.data(), big.size(), fmt, ap);
  va_end(ap);
  out->append(big.data(), static_cast<size_t>(n));
}

std::string FirstLine(const std::string& s, size_t max_len) {
  size_t nl = s.find('\n');
  std::string one = nl == std::string::npos ? s : s.substr(0, nl);
  if (one.size() > max_len) one = one.substr(0, max_len) + "...";
  return one;
}

// M3 考试入口：exam_smoke 同款引导 + LifecycleDriver + 逐 phase/缺口诊断 +
// System.loadLibrary 设备真 loader。返回结构化报告（恒非 null）。
jstring NativeRunExam(JNIEnv* env, jclass, jstring runtime_dex_path, jstring apk_path) {
  const char* boot_path = env->functions->GetStringUTFChars(env, runtime_dex_path, nullptr);
  const char* app_path = env->functions->GetStringUTFChars(env, apk_path, nullptr);
  std::string report;

  using dexvm::framework::ConfigFromManifest;
  using dexvm::framework::LifecycleConfig;
  using dexvm::framework::LifecycleDriver;
  using dexvm::framework::PhaseRecord;
  using dexvm::runtime::ClassLinker;
  using dexvm::runtime::InternTable;
  using dexvm::runtime::VClass;
  using dexvm::runtime::VObject;
  using dexvm::runtime::VMState;

  do {
    if (boot_path == nullptr || app_path == nullptr) {
      report = "FAIL null path argument";
      break;
    }
    const std::string runtime_dex(boot_path);
    const std::string target_apk(app_path);
    const std::string native_lib_dir = DirName(target_apk) + "/nativeLibDir";

    // ---- 设备 loader 上下文（APK lib/<abi> 索引 + nativeLibDir）----
    ExamDeviceContext ctx;
    ctx.native_lib_dir = native_lib_dir;
    ::mkdir(native_lib_dir.c_str(), 0755);
    std::string so_err;
    bool so_index_ok = ctx.so_index.Build(target_apk, &so_err);

    // ---- 引导 VM（exam_smoke 同款：RegisterBuiltins + RegisterFrameworkBuiltins）----
    dexvm::mem::Heap heap;
    InternTable intern;
    ClassLinker linker(&heap, &intern);
    dexvm::runtime::RegisterBuiltins(&linker);
    dexvm::runtime::RegisterFrameworkBuiltins(&linker);
    VMState state;
    state.heap = &heap;
    state.intern = &intern;
    state.linker = &linker;
    // 自动 GC 接线（docs 12 章 22.10）：分配失败→GC 重试链 + OOM 预留单例
    dexvm::gc::GcAttach(state);
    dexvm::runtime::EnsureOomReserve(state);

    // guest JNI 面（InitJNIEnvExt：VMState->JNIEnvExt 登记 + native 派发钩子）
    dexvm::jni::JavaVMExt vm;
    dexvm::jni::InitJavaVMExt(&vm);
    dexvm::jni::JNIEnvExt jni_env;
    dexvm::jni::InitJNIEnvExt(&jni_env, &vm, &state);
    InstallJniTrace(&jni_env);  // JNI 面包屑（诊断；缺口归因用）

    std::string err;
    std::vector<std::unique_ptr<dexvm::session::Session>> sessions;
    std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned_dex;

    // boot classpath：runtime.dex
    {
      auto boot = dexvm::session::Session::Open(runtime_dex, &err);
      if (boot == nullptr || boot->dex_blobs().empty()) {
        report = "FAIL: open runtime.dex: " + err;
        break;
      }
      for (const auto& blob : boot->dex_blobs()) {
        std::string derr;
        auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
        if (dex_file == nullptr) {
          report = "FAIL: parse boot dex " + blob.name + ": " + derr;
          break;
        }
        linker.AddBootDexFile(dex_file.get());
        owned_dex.push_back(std::move(dex_file));
      }
      if (!report.empty()) break;
      sessions.push_back(std::move(boot));
    }

    // 考卷 APK：app loader（18 dex / 梆梆伪加固）
    auto app_session = dexvm::session::Session::Open(target_apk, &err);
    if (app_session == nullptr || app_session->dex_blobs().empty()) {
      report = "FAIL: open target APK: " + err;
      break;
    }
    for (const auto& blob : app_session->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      if (dex_file == nullptr) {
        report = "FAIL: parse app dex " + blob.name + ": " + derr;
        break;
      }
      linker.AddAppDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
    if (report.rfind("FAIL", 0) == 0) break;
    sessions.push_back(std::move(app_session));

    VObject* app_loader = nullptr;
    {
      VClass* cl = linker.FindClass("Ljava/lang/ClassLoader;");
      app_loader = cl != nullptr ? linker.AllocObject(cl) : nullptr;
      if (app_loader == nullptr) {
        report = "FAIL: app loader alloc";
        break;
      }
      linker.loaders().BindObject(app_loader, linker.loaders().default_app());
      // M5 资源面：APK zip 为 app loader 资源源（META-INF/services 等 entry）
      linker.loaders().default_app()->resource_zips.push_back(target_apk);
    }

    // ---- System.loadLibrary 设备钩子（真 loader；exam 结束解除）----
    ctx.vm = &vm;
    ctx.env = &jni_env;
    ctx.state = &state;
    ctx.app_loader = app_loader;
    g_exam = &ctx;
    dexvm::runtime::SetLibraryLoadHook(DeviceLibraryLoader);

    ProgressClear();
    ProgressAppend("target: " + target_apk);
    ProgressAppend("boot: " + runtime_dex);
    // ---- Manifest（考卷身份核对 = 测试数据断言，非壳适配）----
    const dexvm::session::ManifestInfo& mi = sessions.back()->manifest();
    LifecycleConfig cfg = ConfigFromManifest(mi);
    bool identity_ok = cfg.application_class == "com.zhihu.android.app.ZhihuApplication";

    // ---- VM 语义面补全（标准 Android framework API + 虚拟身份，报告记录）----
    std::vector<std::string> sem_added;
    std::vector<Probe> probes;

    // ---- LifecycleDriver 驱动 + 逐 phase 记录 ----
    std::vector<PhaseRecord> records;
    std::vector<std::vector<std::string>> stack_at_failure;
    LifecycleDriver driver(state, app_loader);
    driver.set_observer([&](const std::string& phase, bool begin, const PhaseRecord* rec) {
      if (begin) {
        // messagePump 的 begin 在 PumpLooperQueues 失败后才到达（泵先行、失败
        // 才补记录）——此时 last_throw_backtrace 已是泵内抛出栈，清空会丢诊断；
        // 其余 phase 正常清空隔离。
        if (phase != "messagePump") state.last_throw_backtrace.clear();
        return;
      }
      if (rec == nullptr) return;
      records.push_back(*rec);
      if (!rec->ok) {
        stack_at_failure.push_back(state.last_throw_backtrace);
        ProgressAppend("phase " + phase + " FAIL: " + rec->error);
      } else {
        stack_at_failure.push_back({});
        ProgressAppend("phase " + phase + " ok (" + std::to_string(rec->elapsed_ms) + " ms)");
      }
    });
    // ProviderInfo 预装配/注册诊断实时汇（FileProvider resolveContentProvider 面）
    driver.set_diagnostic_sink(
        [](const std::string& line) { ProgressAppend("preg " + line); });
    // M4.3：guest 执行期 native 崩溃护栏（解释器 SIGSEGV/栈溢出等 -> 可观测
    // 停点，报告照常产出；VM 自身 bug 如实记录为 vm-crash 缺口，不吞）。
    // 覆盖 semface/探针/生命周期链全部 guest 执行面。
    bool ran = false;
    bool vm_crash = false;
    bool vm_signal_crash = false;
    std::string vm_crash_detail;
    std::vector<std::string> crash_frames;  // 崩溃瞬间帧链（innermost 先）
    std::string crash_frame_shape;          // 重复同帧 = 语义死循环；深链独帧 = 语义套娃
    {
      // M4.4：GuestTask 1:1 线程模型（docs 04 章 14.2）——guest 执行整体以
      // main GuestTask 进协程调度器（Thread.start/join、Object.wait 的协程
      // wait/wake 要求 guest 执行处于 guest task 上下文）。
      dexvm::runtime::SetGuestSchedOps(dexvm::runtime::VmSchedBackendOps());
      // 设备考 guest 全程预算放大（默认 60s 只救死锁；SecNeo 解密/业务链深跑
      // 数分钟，60s 命中 = mid-flight 放弃、被误报 "unknown signal"——见
      // GuestRunFailureReason 归因）。30min 仍是 hang 安全网。
      dexvm::runtime::SetGuestRunBudgetMs(30ll * 60 * 1000);
      CrashGuard guard;
      bool ok = guard.Run(
          [&](std::string* e) {
            (void)e;
            return dexvm::runtime::RunGuestMain(state, [&](dexvm::runtime::VMState&) -> bool {
              semface::Complete(state, &linker, mi, target_apk, DirName(target_apk),
                                native_lib_dir, &sem_added);
              RunFaceProbes(state, linker, &probes);
              ran = driver.Run(cfg);
              // 考卷链驱动完成即收卷（guest 进程退出策略，builtins.h
              // GuestSchedOps.request_stop 语义）：调度器残留 guest 线程（juc
              // 线程池常驻 worker）不再把 run_main 拖到 30min 预算——链完成即
              // 产出报告（预算仍兜底中途 hang）。
              if (const dexvm::runtime::GuestSchedOps* sched =
                      dexvm::runtime::GuestSched()) {
                sched->request_stop();
              }
              return true;
            });
          },
          &vm_crash_detail);
      if (!ok) {
        // 崩溃瞬间帧链快照（形态归因：重复同帧 = 语义死循环/终止条件失效；
        // 深链独帧 = 语义套娃/人工递归）。siglongjmp 跳出 run_main，fiber 栈
        // 未回收（调度器随 longjmp 弃置、栈仍映射），frame 指针可读；必须在
        // GuestRuntimeReset 清表前快照。
        const std::vector<dexvm::runtime::Frame*>& fs = state.frame_stack;
        std::map<std::string, int> freq;
        size_t depth = fs.size();
        for (size_t i = depth; i > 0 && depth - i < 48; i--) {
          const dexvm::runtime::Frame* f = fs[i - 1];
          std::string s = "<unknown frame>";
          if (f != nullptr && f->method != nullptr) {
            s = (f->method->declaring != nullptr ? f->method->declaring->descriptor : "?") +
                "." + f->method->name + " (dex_pc=" + std::to_string(f->dex_pc) + ")";
          }
          crash_frames.push_back(s);
          std::string key = f != nullptr && f->method != nullptr ? f->method->name : "?";
          freq[key]++;
        }
        std::string top;
        int top_n = 0;
        for (const auto& [name, n] : freq) {
          if (n > top_n) {
            top_n = n;
            top = name;
          }
        }
        crash_frame_shape = "guest frame depth=" + std::to_string(depth) +
                            ", distinct methods=" + std::to_string(freq.size()) +
                            ", most repeated=" + top + " x" + std::to_string(top_n) +
                            (top_n >= 8 ? " (semantic loop suspect)" : "") +
                            (depth >= 8 && top_n < 8 ? " (nesting/套娃 suspect)" : "");
      }
      // vm-crash（siglongjmp 跳出 run_main）后 guest task 句柄失效：复位防再入
      dexvm::runtime::GuestRuntimeReset(&state);
      if (!ok) {
        vm_crash = true;
        // 归因分流（"unknown signal" 误报修正）：CrashGuard 的 error 只在信号
        // 路径写入（"guest native crash: SEGV_ACCERR @ 0x…"）；空 = 非信号失败
        // （调度器预算中止/未进入），走 GuestRunFailureReason，不再冒充信号崩溃。
        vm_signal_crash = vm_crash_detail.rfind("guest native crash", 0) == 0;
        if (vm_crash_detail.empty()) {
          const char* reason = dexvm::runtime::GuestRunFailureReason();
          vm_crash_detail = std::string("guest execution failed without signal (") +
                            (reason != nullptr && reason[0] != 0 ? reason
                                                                : "RunGuestMain returned false") +
                            ")";
        }
      }
    }
    if (vm_crash) {
      PhaseRecord crash_rec;
      crash_rec.name = vm_signal_crash ? "(vm-crash)" : "(guest-run-aborted)";
      crash_rec.ok = false;
      crash_rec.error = vm_crash_detail + " during guest execution (interpreter)";
      records.push_back(crash_rec);
      stack_at_failure.push_back({});
    }

    // ---- ExceptionInInitializerError 根因 post-mortem ----
    // <clinit> 失败被 EnsureClassInitialized 包装（内层异常被遮蔽）：对失败类的
    // <clinit> 直调复跑取内层异常与抛出栈（诊断面；失败 clinit 通常在首个缺口即
    // 抛，复跑副作用可忽略）。root_cause[record 下标] = {内层诊断, 内层栈}。
    std::map<size_t, std::pair<std::string, std::vector<std::string>>> root_cause;
    if (!vm_crash) {  // vm-crash 后解释器状态不可信，不做 clinit 复跑
    for (size_t i = 0; i < records.size(); i++) {
      if (records[i].ok) continue;
      const std::string& e = records[i].error;
      size_t p = e.find("ExceptionInInitializerError;: ");
      if (p == std::string::npos) continue;
      size_t q = e.find(" <clinit> failed", p);
      if (q == std::string::npos) continue;
      std::string desc =
          e.substr(p + std::strlen("ExceptionInInitializerError;: "), q - p -
               std::strlen("ExceptionInInitializerError;: "));
      VClass* k = linker.FindClass(desc);
      dexvm::runtime::VMethod* clinit = nullptr;
      if (k != nullptr) {
        for (auto& m : k->direct_methods) {
          if (m.name == "<clinit>") {
            clinit = &m;
            break;
          }
        }
      }
      if (clinit == nullptr) continue;
      state.last_throw_backtrace.clear();
      dexvm::runtime::Value rv{};
      // 复跑同样经 guest main fiber（M4.4）：深 clinit 链递归按 4MB fiber 栈
      // 承载，不压 1MB ART 线程栈（溢出根因修复）。
      bool replay_ok = true;
      dexvm::runtime::RunGuestMain(state, [&](dexvm::runtime::VMState& s) -> bool {
        replay_ok = dexvm::runtime::InvokeMethod(s, clinit, nullptr, 0, &rv);
        if (const dexvm::runtime::GuestSchedOps* sched = dexvm::runtime::GuestSched()) {
          sched->request_stop();
        }
        return true;
      });
      dexvm::runtime::GuestRuntimeReset(&state);
      if (!replay_ok && state.HasPendingException()) {
        dexvm::runtime::ObjHeader* ex = state.pending_exception;
        std::string idesc =
            ex != nullptr && ex->klass != nullptr ? ex->klass->descriptor : "Ljava/lang/Throwable;";
        std::string imsg;
        if (ex != nullptr && ex->klass != nullptr) {
          VObject* eo = reinterpret_cast<VObject*>(ex);
          for (VClass* kk = eo->klass; kk != nullptr && imsg.empty(); kk = kk->super_class) {
            for (const auto& f : kk->instance_fields) {
              if (f.name == "detailMessage") {
                dexvm::runtime::ObjHeader* s = nullptr;
                std::memcpy(&s, eo->field_data() + f.offset, sizeof(s));
                if (s != nullptr) {
                  imsg = static_cast<dexvm::runtime::VString*>(s)->ToModifiedUtf8();
                }
                break;
              }
            }
          }
        }
        state.ClearException();
        root_cause[i] = {idesc + ": " + imsg, state.last_throw_backtrace};
      } else {
        state.ClearException();
      }
    }
    }  // if (!vm_crash)

    // ---- 缺口收集（内层根因优先归类；包装异常记录于 diagnosis）----
    std::vector<Gap> gaps;
    for (size_t i = 0; i < records.size(); i++) {
      if (records[i].ok) continue;
      auto it = root_cause.find(i);
      if (it != root_cause.end()) {
        std::vector<Gap> g = ClassifyFailure(records[i].name, it->second.first,
                                             it->second.second);
        for (Gap& gap : g) {
          gap.diagnosis = it->second.first + " (wrapped: " + FirstLine(records[i].error, 160) + ")";
        }
        gaps.insert(gaps.end(), g.begin(), g.end());
      } else {
        std::vector<Gap> g = ClassifyFailure(records[i].name, records[i].error,
                                             stack_at_failure[i]);
        gaps.insert(gaps.end(), g.begin(), g.end());
      }
    }

    // loadLibrary 段：全部尝试均成功（至少一次尝试）
    const std::vector<LibLoadRecord>& loads = ctx.log;
    for (const LibLoadRecord& l : loads) {
      ProgressAppend("loadLibrary " + l.name + (l.ok ? " ok" : " FAIL") + " " + l.detail);
    }
    size_t loads_ok = 0;
    for (const auto& l : loads) {
      if (l.ok) loads_ok++;
    }
    bool loadlib_ok = !loads.empty() && loads_ok == loads.size();

    // 断言底线（M3 设备端）：通过 loadLibrary 段 / 进入 application.attachBaseContext /
    // 唯一卡点为明确记录的宿主特权·环境边界。
    bool entered_attach = false;
    for (const auto& r : records) {
      if (r.name == "application.attachBaseContext") entered_attach = true;
    }
    bool native_boundary_only = !entered_attach && !records.empty() && !records.back().ok;
    if (native_boundary_only) {
      std::vector<Gap> blocking = ClassifyFailure(records.back().name, records.back().error, {});
      for (const Gap& g : blocking) {
        if (g.type != GapType::kNativeLib) native_boundary_only = false;
      }
    }
    bool bottom_ok = entered_attach || loadlib_ok || native_boundary_only;

    const char* verdict = "FAIL";
    if (vm_crash) {
      verdict = vm_signal_crash ? "FAIL (vm-crash)" : "FAIL (guest-run-aborted)";
    } else if (identity_ok) {
      if (entered_attach) verdict = "PASS (attachBaseContext)";
      else if (loadlib_ok) verdict = "PASS (loadlib)";
      else if (native_boundary_only) verdict = "PASS (env-boundary)";
    } else {
      verdict = "FAIL (identity)";
    }

    // ---- 结构化缺口报告 ----
    AppendFmt(&report, "== zhihu exam report (M3 设备化 语义保真验证) ==\n");
    AppendFmt(&report, "target: %s\n", target_apk.c_str());
    AppendFmt(&report, "boot: %s (builtin classes after boot: %zu)\n", runtime_dex.c_str(),
              linker.all_classes().size());
    AppendFmt(&report, "nativeLibDir: %s\n", native_lib_dir.c_str());
    if (so_index_ok) {
      AppendFmt(&report, "apk native libs: %zu entries (%s), %.1f MB, other-abi %d\n",
                ctx.so_index.entries().size(), kAbiDir,
                static_cast<double>(ctx.so_index.abi_total_bytes()) / 1024.0 / 1024.0,
                ctx.so_index.other_abi_count());
    } else {
      AppendFmt(&report, "apk native libs: index FAILED: %s\n", so_err.c_str());
    }
    AppendFmt(&report, "manifest: package=%s application=%s launcher=%s providers=%zu\n",
              mi.package_name.c_str(), cfg.application_class.c_str(),
              cfg.activity_class.c_str(), cfg.providers.size());
    AppendFmt(&report, "identity check: %s\n", identity_ok ? "ok" : "MISMATCH");

    AppendFmt(&report, "\n-- semantic face probes (%zu) --\n", probes.size());
    for (const Probe& p : probes) {
      AppendFmt(&report, "  %s %-40s %s\n", p.ok ? "ok  " : "FAIL", p.name.c_str(),
                p.detail.c_str());
    }

    AppendFmt(&report, "\n-- vm semantic face completion (M3 exam boot, %zu items) --\n",
              sem_added.size());
    for (const std::string& s : sem_added) {
      AppendFmt(&report, "  + %s\n", s.c_str());
    }

    // ProviderInfo 预装配/attach 注册诊断（FileProvider resolveContentProvider 面）
    {
      const auto& regs = driver.provider_registration();
      AppendFmt(&report, "\n-- provider registration (%zu) --\n", regs.size());
      for (const std::string& s : regs) {
        AppendFmt(&report, "  %s\n", s.c_str());
        ProgressAppend("preg " + s);
      }
    }

    AppendFmt(&report, "\n-- native library execution (%zu loads, %zu ok) --\n", loads.size(),
              loads_ok);
    for (size_t i = 0; i < loads.size(); i++) {
      const LibLoadRecord& l = loads[i];
      AppendFmt(&report, "  [%zu] %s -> %s\n", i + 1, l.name.c_str(), l.path.c_str());
      if (!l.extract.empty()) AppendFmt(&report, "      %s\n", l.extract.c_str());
      AppendFmt(&report, "      %s %s\n", l.ok ? "ok  " : "FAIL", l.detail.c_str());
    }

    // RegisterNatives 生效证据：JNI_OnLoad 登记的 native 方法清单
    {
      std::vector<std::string> natives;
      for (dexvm::runtime::VClass* k : linker.all_classes()) {
        for (const auto& m : k->direct_methods) {
          if (m.jni_native_fn != nullptr) {
            natives.push_back(k->descriptor + "." + m.name + m.shorty + " (direct)");
          }
        }
        for (const auto& m : k->virtual_methods) {
          if (m.jni_native_fn != nullptr) {
            natives.push_back(k->descriptor + "." + m.name + m.shorty + " (virtual)");
          }
        }
      }
      AppendFmt(&report, "\n-- registered native methods (%zu, RegisterNatives) --\n",
                natives.size());
      for (const std::string& s : natives) {
        AppendFmt(&report, "  %s\n", s.c_str());
      }
    }

    // JNI 面包屑尾部（诊断：guest native 崩溃前的 FindClass/Get*ID/字段链）
    {
      const std::vector<std::string>& tr = ctx.jni_trace;
      size_t show = tr.size() > 40 ? 40 : tr.size();
      AppendFmt(&report, "\n-- jni trace (last %zu of %zu in ring; %zu total) --\n", show, tr.size(),
                ctx.jni_trace_calls);
      for (size_t i = tr.size() - show; i < tr.size(); i++) {
        AppendFmt(&report, "  %s\n", tr[i].c_str());
      }
    }

    AppendFmt(&report, "\n-- phase sequence (%zu, chain %s) --\n", records.size(),
              ran ? "completed" : "fail-stop");
    for (size_t i = 0; i < records.size(); i++) {
      AppendFmt(&report, "  [%zu] %-32s %s %lldms\n", i + 1, records[i].name.c_str(),
                records[i].ok ? "ok  " : "FAIL",
                static_cast<long long>(records[i].elapsed_ms));
      if (!records[i].ok) {
        AppendFmt(&report, "        diagnosis: %s\n",
                  FirstLine(records[i].error, 400).c_str());
      }
    }
    AppendFmt(&report, "stop point: %s\n",
              records.empty() ? "(none)" : records.back().name.c_str());
    if (!records.empty() && !records.back().ok) {
      AppendFmt(&report, "stop diagnosis: %s\n", records.back().error.c_str());
    }
    if (!crash_frames.empty()) {
      // 崩溃帧链形态（归因：同帧重复 = 语义死循环；深链独帧 = 语义套娃）
      AppendFmt(&report, "\n-- crash frame chain (%zu innermost of %s) --\n", crash_frames.size(),
                crash_frame_shape.c_str());
      for (const std::string& f : crash_frames) {
        AppendFmt(&report, "  at %s\n", f.c_str());
      }
    }

    AppendFmt(&report, "\n-- gap list (%zu entries) --\n", gaps.size());
    for (size_t i = 0; i < gaps.size(); i++) {
      const Gap& g = gaps[i];
      AppendFmt(&report, "  #%zu [%s] %s\n", i + 1, GapTypeName(g.type), g.target.c_str());
      AppendFmt(&report, "      caller: %s\n", g.caller.c_str());
      AppendFmt(&report, "      diagnosis: %s\n", g.diagnosis.c_str());
      for (size_t f = 0; f < g.stack.size() && f < 12; f++) {
        AppendFmt(&report, "        at %s\n", g.stack[f].c_str());
      }
      AppendFmt(&report, "      fix: %s\n", g.fix.c_str());
    }
    // 分类统计（unique target）
    std::map<std::string, int> by_type;
    std::map<std::string, int> unique;
    for (const Gap& g : gaps) {
      by_type[GapTypeName(g.type)]++;
      unique[std::string(GapTypeName(g.type)) + " " + g.target]++;
    }
    AppendFmt(&report, "\n-- gap summary --\n");
    for (const auto& [k, v] : by_type) {
      AppendFmt(&report, "  %-22s %d\n", k.c_str(), v);
    }
    AppendFmt(&report, "  unique (type,target)   %zu\n", unique.size());

    AppendFmt(&report, "\n-- bottom line --\n");
    if (vm_crash) {
      AppendFmt(&report, "%s (%s)\n",
                vm_signal_crash ? "vm native crash: YES" : "guest run aborted (no signal)",
                vm_crash_detail.c_str());
    }
    // 堆水位 + 自动 GC 观测（docs 12 章 22.10 AllocateInternalWithGc 重试链）：
    // gc 轮数/累计回收/重试链救回次数。裸兜底异常诊断面：klass==null 兜底 =
    // 异常类缺失或堆分配失败；used≈capacity 即 OOM 归因（自动 GC 耗尽 =
    // 存活集超容量）。
    AppendFmt(&report, "heap: %.1f MB used / %zu MB cap, live %zu, gc %zu runs (%.1f MB freed, %zu alloc recovered)\n",
              heap.bytes_allocated() / (1024.0 * 1024.0), heap.capacity() / (1024 * 1024),
              heap.live_count(), heap.gc_runs(), heap.gc_freed_bytes() / (1024.0 * 1024.0),
              heap.alloc_recovered());
    AppendFmt(&report, "loadLibrary segment passed: %s (%zu loads, %zu ok)\n",
              loadlib_ok ? "YES" : "NO", loads.size(), loads_ok);
    AppendFmt(&report, "entered application.attachBaseContext: %s\n",
              entered_attach ? "YES" : "NO");
    if (!entered_attach && native_boundary_only) {
      AppendFmt(&report,
                "blocked at native boundary: environment/privilege limitation "
                "(docs 29.2)\n");
    }
    AppendFmt(&report, "last phase: %s\n",
              records.empty() ? "(none)" : records.back().name.c_str());
    AppendFmt(&report, "RESULT: %s\n", verdict);
    (void)bottom_ok;

    // 解除设备钩子（上下文随本调用销毁）
    dexvm::runtime::SetLibraryLoadHook(nullptr);
    g_exam = nullptr;
  } while (false);

  if (boot_path != nullptr) env->functions->ReleaseStringUTFChars(env, runtime_dex_path, boot_path);
  if (app_path != nullptr) env->functions->ReleaseStringUTFChars(env, apk_path, app_path);
  return env->functions->NewStringUTF(env, report.c_str());
}

// =====================================================================
// target-app 考卷 8 轮编排（nativeRunTargetExam）：
// tests/target_exam_table.h 哨兵契约表（R1-R6/R8 表驱动）+ R4 直跑线任务
// （File/Io/Monitor/Socket/Http panel 断言）+ R6 生命周期链/PM/Bundle-Intent
// 计分面 + R7 桥接出画断言（ViewSnapshot revision/首帧标记/guest 文本）。
// R8 = 真实 app 路径：guest System.loadLibrary（target-app
// HardenedNativeLoader static 块）-> VM LoadLibraryCommon -> 设备
// LibraryLoadHook -> APK lib/<abi> 提取 -> dlopen（fixture DT_NEEDED 带依赖
// payload）-> JNI_OnLoad（构造 cookie/TracerPid/dladdr/版本协商）->
// register() RegisterNatives a-g -> Java 侧哨兵断言（581-587）。
// 报告：逐轮 [Rn] PASS/FAIL 行（tools/target_exam_tests.sh logcat 解析）。
// =====================================================================

using dexvm::runtime::ClassLinker;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;
using dexvm::runtime::VMState;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;

// 实例字段按名取（超类链回溯；RuntimePanel.label 等私有字段不设障——VM 视角
// 只有字段表，无 Java 访问控制语义）。
VObject* ExamFieldRef(VObject* o, const char* name) {
  if (o == nullptr || o->klass == nullptr) return nullptr;
  for (VClass* k = o->klass; k != nullptr; k = k->super_class) {
    for (const auto& f : k->instance_fields) {
      if (f.name == name) {
        ObjHeader* v = nullptr;
        std::memcpy(&v, o->field_data() + f.offset, sizeof(v));
        return static_cast<VObject*>(v);
      }
    }
  }
  return nullptr;
}

std::string ExamFieldString(VObject* o, const char* name) {
  ObjHeader* s = ExamFieldRef(o, name);
  return s != nullptr ? static_cast<VString*>(s)->ToModifiedUtf8() : std::string();
}

VString* ExamStr(ClassLinker* linker, const std::string& s) {
  std::u16string u16(s.begin(), s.end());
  return linker->AllocString(u16);
}

// 通用调用（self 显式；实例方法自动前置 self；失败 diag = 异常链）。
bool ExamInvoke(VMState& state, ClassLinker* linker, VObject* app_loader, const char* owner,
                const char* method, std::vector<std::string> params, const char* ret,
                bool is_static, VObject* self, const Value* args, size_t argc, Value* out,
                std::string* diag) {
  VClass* k = linker->FindClass(&state, app_loader, owner);
  if (k == nullptr) {
    *diag = "class not found: " + std::string(owner);
    return false;
  }
  if (!linker->InitializeClass(k, &state)) {
    *diag = "clinit failed: " + DescribeGuestException(state.pending_exception);
    state.ClearException();
    return false;
  }
  VMethod* m = nullptr;
  for (auto* list : {&k->direct_methods, &k->virtual_methods}) {
    for (auto& mm : *list) {
      if (mm.name == method && mm.IsStatic() == is_static && mm.param_types == params &&
          mm.return_type == ret) {
        m = &mm;
        break;
      }
    }
    if (m != nullptr) break;
  }
  if (m == nullptr) {
    *diag = "method not found: " + std::string(owner) + "." + method;
    return false;
  }
  Value argv[8];
  size_t n = 0;
  if (!is_static) argv[n++].ref = self;
  for (size_t i = 0; i < argc && n < 8; i++) argv[n++] = args[i];
  if (!InvokeMethod(state, m, n > 0 ? argv : nullptr, n, out)) {
    *diag = DescribeGuestException(state.pending_exception);
    state.ClearException();
    return false;
  }
  return true;
}

// 表驱动探针（self 构造 + int 形参；返回哨兵 int）。
bool ExamTableProbe(VMState& state, ClassLinker* linker, VObject* app_loader,
                    const dexvm::tests::TargetExamProbe& p, int32_t* got, std::string* diag) {
  VClass* k = linker->FindClass(&state, app_loader, p.owner);
  if (k == nullptr) {
    *diag = "class not found: " + std::string(p.owner);
    return false;
  }
  if (!linker->InitializeClass(k, &state)) {
    *diag = "clinit failed: " + DescribeGuestException(state.pending_exception);
    state.ClearException();
    return false;
  }
  std::vector<std::string> params;
  if (p.p0 != nullptr) params.push_back(p.p0);
  if (p.p1 != nullptr) params.push_back(p.p1);
  VMethod* m = nullptr;
  for (auto* list : {&k->direct_methods, &k->virtual_methods}) {
    for (auto& mm : *list) {
      if (mm.name == p.method && mm.IsStatic() == p.is_static && mm.param_types == params &&
          mm.return_type == p.ret) {
        m = &mm;
        break;
      }
    }
    if (m != nullptr) break;
  }
  if (m == nullptr) {
    *diag = "method not found: " + std::string(p.owner) + "." + p.method;
    return false;
  }
  Value argv[4];
  size_t argc = 0;
  if (!p.is_static) {
    VObject* self = linker->AllocObject(k);
    if (self == nullptr) {
      *diag = "alloc failed";
      return false;
    }
    VMethod* ctor = nullptr;
    for (auto& mm : k->direct_methods) {
      if (mm.name == "<init>" && !mm.IsStatic() && mm.param_types.empty() &&
          mm.return_type == "V") {
        ctor = &mm;
        break;
      }
    }
    if (ctor != nullptr) {
      Value cself{};
      cself.ref = self;
      Value cr{};
      if (!InvokeMethod(state, ctor, &cself, 1, &cr)) {
        *diag = "ctor failed: " + DescribeGuestException(state.pending_exception);
        state.ClearException();
        return false;
      }
    }
    argv[argc++].ref = self;
  }
  if (p.p0 != nullptr) {
    argv[argc] = Value{};
    if (std::strcmp(p.p0, "I") == 0) argv[argc].i = p.a0;
    argc++;
  }
  if (p.p1 != nullptr) {
    argv[argc] = Value{};
    if (std::strcmp(p.p1, "I") == 0) argv[argc].i = p.a1;
    argc++;
  }
  Value r{};
  if (!InvokeMethod(state, m, argc > 0 ? argv : nullptr, argc, &r)) {
    *diag = DescribeGuestException(state.pending_exception);
    state.ClearException();
    return false;
  }
  *got = r.i;
  return true;
}

// guest 线程（java.lang.Thread）起停辅助——R4 直跑线任务按 MainActivity
// 同形（Runnable + Thread.start）驱动。
VObject* ExamStartThread(VMState& state, ClassLinker* linker, VObject* runnable,
                         const char* name, std::string* diag) {
  VClass* tk = linker->FindClass("Ljava/lang/Thread;");
  if (tk == nullptr) {
    *diag = "no java.lang.Thread";
    return nullptr;
  }
  VObject* t = linker->AllocObject(tk);
  if (t == nullptr) {
    *diag = "thread alloc failed";
    return nullptr;
  }
  Value cargs[2] = {};
  cargs[0].ref = runnable;
  cargs[1].ref = ExamStr(linker, name);
  Value cr{};
  if (!ExamInvoke(state, linker, nullptr, "Ljava/lang/Thread;", "<init>",
                  {"Ljava/lang/Runnable;", "Ljava/lang/String;"}, "V", false, t, cargs, 2, &cr,
                  diag)) {
    return nullptr;
  }
  Value sv{};
  if (!ExamInvoke(state, linker, nullptr, "Ljava/lang/Thread;", "start", {}, "V", false, t,
                  nullptr, 0, &sv, diag)) {
    return nullptr;
  }
  return t;
}

void ExamJoinThread(VMState& state, ClassLinker* linker, VObject* thread, long long ms) {
  if (thread == nullptr) return;
  std::string diag;
  Value a{};
  a.l = ms;
  Value r{};
  ExamInvoke(state, linker, nullptr, "Ljava/lang/Thread;", "join", {"J"}, "V", false, thread, &a,
             1, &r, &diag);
}

struct ExamCheck {
  int round = 0;
  std::string name;
  bool ok = false;
  std::string detail;
};

// panel label 文本断言（前缀包含）。
void ExamTaskCheck(std::vector<ExamCheck>* checks, int round, const char* name,
                   VObject* label_obj, const char* expected_prefix) {
  ExamCheck c;
  c.round = round;
  c.name = name;
  std::string text = ExamFieldString(label_obj, "mText");
  c.ok = text.find(expected_prefix) != std::string::npos;
  c.detail = "text=\"" + text + "\" expect prefix \"" + expected_prefix + "\"";
  checks->push_back(std::move(c));
}

jstring NativeRunTargetExam(JNIEnv* env, jclass, jstring runtime_dex_path, jstring apk_path,
                            jstring round_sel) {
  const char* boot_path = env->functions->GetStringUTFChars(env, runtime_dex_path, nullptr);
  const char* app_path = env->functions->GetStringUTFChars(env, apk_path, nullptr);
  const char* rounds_c = env->functions->GetStringUTFChars(env, round_sel, nullptr);
  std::string report;

  using dexvm::runtime::ClassLinker;
  using dexvm::runtime::InternTable;
  using dexvm::runtime::VClass;
  using dexvm::runtime::VObject;
  using dexvm::runtime::VMState;

  do {
    if (boot_path == nullptr || app_path == nullptr) {
      report = "FAIL null path argument";
      break;
    }
    const std::string runtime_dex(boot_path);
    const std::string target_apk(app_path);
    const std::string round_text = rounds_c != nullptr ? rounds_c : "all";
    const std::string native_lib_dir = DirName(target_apk) + "/nativeLibDir";
    const std::string files_dir = DirName(target_apk);

    // 轮次选择（"all" / "1,3" / "R2"）
    bool round_on[9] = {};
    if (round_text == "all" || round_text.empty()) {
      for (int r = 1; r <= 8; r++) round_on[r] = true;
    } else {
      for (size_t i = 0; i < round_text.size();) {
        size_t comma = round_text.find(',', i);
        std::string tok = round_text.substr(
            i, comma == std::string::npos ? std::string::npos : comma - i);
        if (tok.rfind("R", 0) == 0) tok = tok.substr(1);
        int r = std::atoi(tok.c_str());
        if (r >= 1 && r <= 8) round_on[r] = true;
        if (comma == std::string::npos) break;
        i = comma + 1;
      }
    }

    // netty HTTP 服务面：1s 自闭（fastHttpServerFlow 同型；进程级 setenv）
    ::setenv("DEXVM_HTTP_SECONDS", "1", 1);

    ProgressClear();
    ProgressAppend("target exam: " + target_apk + " rounds=" + round_text);
    ExamDeviceContext ctx;
    ctx.native_lib_dir = native_lib_dir;
    ::mkdir(native_lib_dir.c_str(), 0755);
    std::string so_err;
    bool so_index_ok = ctx.so_index.Build(target_apk, &so_err);

    // ---- 引导 VM（apk_run 引导链：boot=runtime.dex，app=target-app APK）----
    dexvm::mem::Heap heap;
    InternTable intern;
    ClassLinker linker(&heap, &intern);
    dexvm::runtime::RegisterBuiltins(&linker);
    dexvm::runtime::RegisterFrameworkBuiltins(&linker);
    VMState state;
    state.heap = &heap;
    state.intern = &intern;
    state.linker = &linker;
    dexvm::gc::GcAttach(state);
    dexvm::runtime::EnsureOomReserve(state);

    dexvm::jni::JavaVMExt vm;
    dexvm::jni::InitJavaVMExt(&vm);
    dexvm::jni::JNIEnvExt jni_env;
    dexvm::jni::InitJNIEnvExt(&jni_env, &vm, &state);
    InstallJniTrace(&jni_env);

    std::string err;
    std::vector<std::unique_ptr<dexvm::session::Session>> sessions;
    std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned_dex;

    {
      auto boot = dexvm::session::Session::Open(runtime_dex, &err);
      if (boot == nullptr || boot->dex_blobs().empty()) {
        report = "FAIL: open runtime.dex: " + err;
        break;
      }
      for (const auto& blob : boot->dex_blobs()) {
        std::string derr;
        auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
        if (dex_file == nullptr) {
          report = "FAIL: parse boot dex " + blob.name + ": " + derr;
          break;
        }
        linker.AddBootDexFile(dex_file.get());
        owned_dex.push_back(std::move(dex_file));
      }
      if (!report.empty()) break;
      sessions.push_back(std::move(boot));
    }
    // R7 首帧管线：boot dex 装载后登记（lib widget 类此时可 Find）。
    dexvm::framework::RegisterRenderPipeline(&linker);
    dexvm::runtime::ApplyFrameworkApiStubs(&linker);  // lib 类 API 补桩第二遍

    auto app_session = dexvm::session::Session::Open(target_apk, &err);
    if (app_session == nullptr || app_session->dex_blobs().empty()) {
      report = "FAIL: open target APK: " + err;
      break;
    }
    for (const auto& blob : app_session->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      if (dex_file == nullptr) {
        report = "FAIL: parse app dex " + blob.name + ": " + derr;
        break;
      }
      linker.AddAppDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
    if (report.rfind("FAIL", 0) == 0) break;
    sessions.push_back(std::move(app_session));

    VObject* app_loader = nullptr;
    {
      VClass* cl = linker.FindClass("Ljava/lang/ClassLoader;");
      app_loader = cl != nullptr ? linker.AllocObject(cl) : nullptr;
      if (app_loader == nullptr) {
        report = "FAIL: app loader alloc";
        break;
      }
      linker.loaders().BindObject(app_loader, linker.loaders().default_app());
      linker.loaders().default_app()->resource_zips.push_back(target_apk);
    }

    // System.loadLibrary 设备钩子（R8 真实 app 路径落点；考卷结束解除）
    ctx.vm = &vm;
    ctx.env = &jni_env;
    ctx.state = &state;
    ctx.app_loader = app_loader;
    g_exam = &ctx;
    dexvm::runtime::SetLibraryLoadHook(DeviceLibraryLoader);

    dexvm::runtime::SetGuestSchedOps(dexvm::runtime::VmSchedBackendOps());
    dexvm::runtime::SetGuestRunBudgetMs(30ll * 60 * 1000);

    std::vector<ExamCheck> checks;   // 专检（R4/R6/R7）
    std::string task_line_detail;
    std::string lifecycle_log;
    std::string phase_seq;
    dexvm::bridge::UIBridge ui;
    bool ran_all = false;
    bool vm_crash = false;
    std::string vm_crash_detail;

    // 考卷起点复位 UI 桥接 mirror（上一会话/宿主首帧的 baseline 会让本次首帧
    // present 变成 patch——考卷断言需要全量首帧树）
    dexvm::bridge::ResetUiBridgeProtocol();
    {
      CrashGuard guard;
      bool ok = guard.Run(
          [&](std::string* e) {
            (void)e;
            return dexvm::runtime::RunGuestMain(state, [&](dexvm::runtime::VMState& s) -> bool {
              // -- 宿主身份/Context 面补齐（PackageManager/ApplicationInfo/资源链；
              //    R6 计分面依赖；与 nativeRunExam 同一引导口径 semface::Complete）--
              {
                std::vector<std::string> sem_added;
                semface::Complete(s, &linker, sessions.back()->manifest(), target_apk, files_dir,
                                  native_lib_dir, &sem_added);
                // 临时目录投影为宿主 files 目录（Android app 可写面；AOSP
                // java.io.tmpdir=/data/local/tmp 对 app 不可写）
                VClass* sys_cls = linker.FindClass("Ljava/lang/System;");
                if (sys_cls != nullptr) {
                  VMethod* setp = nullptr;
                  for (auto& m : sys_cls->direct_methods) {
                    if (m.name == "setProperty" && m.IsStatic() && m.param_types.size() == 2 &&
                        m.param_types[0] == "Ljava/lang/String;" &&
                        m.param_types[1] == "Ljava/lang/String;") {
                      setp = &m;
                      break;
                    }
                  }
                  if (setp != nullptr) {
                    Value sa[2] = {};
                    sa[0].ref = linker.AllocString(u"java.io.tmpdir");
                    sa[1].ref = linker.AllocString(
                        std::u16string(files_dir.begin(), files_dir.end()));
                    Value sr{};
                    dexvm::runtime::InvokeMethod(s, setp, sa, 2, &sr);
                  }
                }
              }
              // ---- 表驱动哨兵（R1-R6/R8；R4 mappedBuffer、R6 intent/binder、R8 a-g）----
              for (int i = 0; i < dexvm::tests::kTargetExamProbeCount; i++) {
                const dexvm::tests::TargetExamProbe& p = dexvm::tests::kTargetExamProbes[i];
                if (!round_on[p.round]) continue;
                int32_t got = 0;
                std::string diag;
                bool pok = ExamTableProbe(s, &linker, app_loader, p, &got, &diag);
                bool pass = pok && got == p.expect;
                AppendFmt(&report, "%s R%d %s.%s [%s]", pass ? "PASS:" : "FAIL:", p.round,
                          p.owner, p.method, p.note);
                if (pok && !pass) AppendFmt(&report, " got %d expect %d", got, p.expect);
                if (!pok) AppendFmt(&report, " diag: %s", diag.c_str());
                report += "\n";
                ProgressAppend(std::string(pass ? "PASS " : "FAIL ") + "R" +
                               std::to_string(p.round) + " " + p.owner + "." + p.method +
                               (pok ? "" : " diag: " + diag));
                ExamCheck c;
                c.round = p.round;
                c.name = std::string(p.owner) + "." + p.method;
                c.ok = pass;
                c.detail = pok ? ("got " + std::to_string(got)) : diag;
                checks.push_back(std::move(c));
              }

              // ---- R4 直跑线任务（MainActivity 同形：Runnable + Thread）----
              if (round_on[4]) {
                std::string diag;
                Value pr{};
                VClass* panel_k = linker.FindClass(&s, app_loader, "Lcom/coke/target/RuntimePanel;");
                VObject* panel = panel_k != nullptr ? linker.AllocObject(panel_k) : nullptr;
                Value ctx_null{};
                bool panel_ok =
                    panel != nullptr &&
                    ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimePanel;", "<init>",
                               {"Landroid/content/Context;"}, "V", false, panel, &ctx_null, 1,
                               &pr, &diag);
                VClass* file_k = linker.FindClass("Ljava/io/File;");
                VObject* file = file_k != nullptr ? linker.AllocObject(file_k) : nullptr;
                Value fargs[1] = {};
                fargs[0].ref = ExamStr(&linker, files_dir);
                bool file_ok =
                    file != nullptr &&
                    ExamInvoke(s, &linker, nullptr, "Ljava/io/File;", "<init>",
                               {"Ljava/lang/String;"}, "V", false, file, fargs, 1, &pr, &diag);
                ExamBreadcrumb("taskline setup panel=%d file=%d", panel_ok ? 1 : 0,
                               file_ok ? 1 : 0);
                if (!panel_ok || !file_ok) {
                  ExamCheck c;
                  c.round = 4;
                  c.name = "taskline-setup";
                  c.ok = false;
                  c.detail = diag;
                  checks.push_back(std::move(c));
                } else {
                  // FileTask（同步直跑：run() 在主 guest 任务）
                  std::string fd_diag;
                  VClass* ft_k = linker.FindClass(&s, app_loader, "Lcom/coke/target/RuntimeFileTask;");
                  VObject* ft = ft_k != nullptr ? linker.AllocObject(ft_k) : nullptr;
                  Value targs[2] = {};
                  targs[0].ref = panel;
                  targs[1].ref = file;
                  bool ft_ok =
                      ft != nullptr &&
                      ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeFileTask;",
                                 "<init>", {"Lcom/coke/target/RuntimePanel;", "Ljava/io/File;"},
                                 "V", false, ft, targs, 2, &pr, &fd_diag) &&
                      ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeFileTask;",
                                 "run", {}, "V", false, ft, nullptr, 0, &pr, &fd_diag);
                  ExamCheck c;
                  c.round = 4;
                  c.name = "taskline-file";
                  c.ok = ft_ok;
                  c.detail = ft_ok ? "run ok" : fd_diag;
                  checks.push_back(std::move(c));
                  ExamBreadcrumb("taskline file done ok=%d", ft_ok ? 1 : 0);
                  ExamTaskCheck(&checks, 4, "taskline-file-text",
                                ExamFieldRef(panel, "fileLabel"), "Guest files");

                  // IoTask：createPipe + reader/writer 双线程 -> ioStatus TextView
                  VClass* tv_k = linker.FindClass("Landroid/widget/TextView;");
                  VObject* io_label = tv_k != nullptr ? linker.AllocObject(tv_k) : nullptr;
                  ExamInvoke(s, &linker, app_loader, "Landroid/widget/TextView;", "<init>",
                             {"Landroid/content/Context;"}, "V", false, io_label, &ctx_null, 1,
                             &pr, &diag);
                  ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeIoTask;",
                             "createPipe", {}, "V", true, nullptr, nullptr, 0, &pr, &diag);
                  VClass* io_k = linker.FindClass(&s, app_loader, "Lcom/coke/target/RuntimeIoTask;");
                  VObject* io_reader = io_k != nullptr ? linker.AllocObject(io_k) : nullptr;
                  VObject* io_writer = io_k != nullptr ? linker.AllocObject(io_k) : nullptr;
                  Value iargs_r[2] = {};
                  iargs_r[0].ref = io_label;
                  iargs_r[1].i = 0;
                  Value iargs_w[2] = {};
                  iargs_w[0].ref = io_label;
                  iargs_w[1].i = 1;
                  ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeIoTask;", "<init>",
                             {"Landroid/widget/TextView;", "Z"}, "V", false, io_reader, iargs_r,
                             2, &pr, &diag);
                  ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeIoTask;", "<init>",
                             {"Landroid/widget/TextView;", "Z"}, "V", false, io_writer, iargs_w,
                             2, &pr, &diag);
                  VObject* io_r_thread =
                      ExamStartThread(s, &linker, io_reader, "exam-io-reader", &diag);
                  VObject* io_w_thread =
                      ExamStartThread(s, &linker, io_writer, "exam-io-writer", &diag);
                  ExamJoinThread(s, &linker, io_w_thread, 8000);
                  ExamJoinThread(s, &linker, io_r_thread, 8000);
                  ExamBreadcrumb("taskline io done");
                  ExamTaskCheck(&checks, 4, "taskline-io", io_label, "Guest pipe IO");

                  // MonitorTask：waiter + notifier -> panel.monitorLabel
                  VClass* mon_k =
                      linker.FindClass(&s, app_loader, "Lcom/coke/target/RuntimeMonitorTask;");
                  VObject* mon_w = mon_k != nullptr ? linker.AllocObject(mon_k) : nullptr;
                  VObject* mon_n = mon_k != nullptr ? linker.AllocObject(mon_k) : nullptr;
                  Value margs_w[2] = {};
                  margs_w[0].ref = panel;
                  margs_w[1].i = 0;
                  Value margs_n[2] = {};
                  margs_n[0].ref = panel;
                  margs_n[1].i = 1;
                  ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeMonitorTask;",
                             "<init>", {"Lcom/coke/target/RuntimePanel;", "Z"}, "V", false, mon_w,
                             margs_w, 2, &pr, &diag);
                  ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeMonitorTask;",
                             "<init>", {"Lcom/coke/target/RuntimePanel;", "Z"}, "V", false, mon_n,
                             margs_n, 2, &pr, &diag);
                  VObject* mon_w_thread =
                      ExamStartThread(s, &linker, mon_w, "exam-monitor-waiter", &diag);
                  VObject* mon_n_thread =
                      ExamStartThread(s, &linker, mon_n, "exam-monitor-notifier", &diag);
                  ExamJoinThread(s, &linker, mon_n_thread, 8000);
                  ExamJoinThread(s, &linker, mon_w_thread, 8000);
                  ExamBreadcrumb("taskline monitor done");
                  ExamTaskCheck(&checks, 4, "taskline-monitor",
                                ExamFieldRef(panel, "monitorLabel"), "Guest monitor: 73");

                  // SocketTask：server + client -> panel.socketLabel（"Guest socket: "）
                  VClass* sock_k =
                      linker.FindClass(&s, app_loader, "Lcom/coke/target/RuntimeSocketTask;");
                  VObject* sock_s = sock_k != nullptr ? linker.AllocObject(sock_k) : nullptr;
                  VObject* sock_c = sock_k != nullptr ? linker.AllocObject(sock_k) : nullptr;
                  Value sargs_s[2] = {};
                  sargs_s[0].ref = panel;
                  sargs_s[1].i = 1;
                  Value sargs_c[2] = {};
                  sargs_c[0].ref = panel;
                  sargs_c[1].i = 0;
                  ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeSocketTask;",
                             "<init>", {"Lcom/coke/target/RuntimePanel;", "Z"}, "V", false,
                             sock_s, sargs_s, 2, &pr, &diag);
                  ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeSocketTask;",
                             "<init>", {"Lcom/coke/target/RuntimePanel;", "Z"}, "V", false,
                             sock_c, sargs_c, 2, &pr, &diag);
                  VObject* sock_s_thread =
                      ExamStartThread(s, &linker, sock_s, "exam-socket-server", &diag);
                  VObject* sock_c_thread =
                      ExamStartThread(s, &linker, sock_c, "exam-socket-client", &diag);
                  ExamJoinThread(s, &linker, sock_c_thread, 10000);
                  ExamJoinThread(s, &linker, sock_s_thread, 10000);
                  ExamBreadcrumb("taskline socket done");
                  ExamTaskCheck(&checks, 4, "taskline-socket",
                                ExamFieldRef(panel, "socketLabel"), "Guest socket");

                  // HttpTask：手写 HTTP/1.1 server + client -> panel.httpLabel
                  VClass* http_k =
                      linker.FindClass(&s, app_loader, "Lcom/coke/target/RuntimeHttpTask;");
                  VObject* http_s = http_k != nullptr ? linker.AllocObject(http_k) : nullptr;
                  VObject* http_c = http_k != nullptr ? linker.AllocObject(http_k) : nullptr;
                  Value hargs_s[2] = {};
                  hargs_s[0].ref = panel;
                  hargs_s[1].i = 1;
                  Value hargs_c[2] = {};
                  hargs_c[0].ref = panel;
                  hargs_c[1].i = 0;
                  ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeHttpTask;",
                             "<init>", {"Lcom/coke/target/RuntimePanel;", "Z"}, "V", false,
                             http_s, hargs_s, 2, &pr, &diag);
                  ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeHttpTask;",
                             "<init>", {"Lcom/coke/target/RuntimePanel;", "Z"}, "V", false,
                             http_c, hargs_c, 2, &pr, &diag);
                  VObject* http_s_thread =
                      ExamStartThread(s, &linker, http_s, "exam-http-server", &diag);
                  VObject* http_c_thread =
                      ExamStartThread(s, &linker, http_c, "exam-http-client", &diag);
                  ExamJoinThread(s, &linker, http_c_thread, 10000);
                  ExamJoinThread(s, &linker, http_s_thread, 10000);
                  ExamBreadcrumb("taskline http done");
                  ExamTaskCheck(&checks, 4, "taskline-http",
                                ExamFieldRef(panel, "httpLabel"), "Guest HTTP");
                }
              }

              // ---- R6 生命周期链（AOSP 顺序；lifecycleLog token 契约）+ R7 首帧 ----
              if (round_on[6] || round_on[7]) {
                auto cfg = dexvm::framework::ConfigFromManifest(sessions.back()->manifest());
                dexvm::framework::LifecycleDriver driver(s, app_loader);
                AttachProgressObserver(driver);
                driver.set_first_frame_hook([&](dexvm::runtime::VObject* activity,
                                                const dexvm::framework::PhaseRunner& run_phase,
                                                std::string* error) {
                  return dexvm::framework::RunFirstFramePipeline(s, activity, &ui,
                                                                 kGuestWindowWidth,
                                                                 kGuestWindowHeight, run_phase,
                                                                 error);
                });
                bool ran = driver.Run(cfg);
                for (const auto& p : driver.phases()) {
                  phase_seq += p.name + " ";
                }
                if (round_on[6]) {
                  ExamCheck c;
                  c.round = 6;
                  c.name = "lifecycle-chain";
                  c.ok = ran;
                  c.detail = ran ? phase_seq : driver.error();
                  checks.push_back(std::move(c));

                  // lifecycleLog token 序（字段名契约；lifecycle_smoke 同口径）
                  VClass* app_cls =
                      linker.FindClass(dexvm::session::DotToDescriptor(cfg.application_class));
                  for (VClass* k = app_cls; k != nullptr && lifecycle_log.empty();
                       k = k->super_class) {
                    for (const auto& f : k->static_fields) {
                      if (f.name == "lifecycleLog" && f.is_static &&
                          f.offset < app_cls->static_storage.size()) {
                        const dexvm::runtime::Value& slot = app_cls->static_storage[f.offset];
                        if (slot.ref != nullptr) {
                          lifecycle_log =
                              static_cast<dexvm::runtime::VString*>(slot.ref)->ToModifiedUtf8();
                        }
                        break;
                      }
                    }
                  }
                  ExamCheck c2;
                  c2.round = 6;
                  c2.name = "lifecycleLog-tokens";
                  const char* want =
                      "attachBaseContext;provider.onCreate;onCreate;activity.onCreate;"
                      "activity.onStart;activity.onResume;";
                  c2.ok = lifecycle_log == want;
                  c2.detail = "log=\"" + lifecycle_log + "\"";
                  checks.push_back(std::move(c2));

                  VObject* activity = driver.activity();
                  Value pctx{};
                  pctx.ref = activity;
                  Value pm{};
                  std::string diag;
                  // fixture 实例方法面：new RuntimeLibraryCompatibilityFixture()
                  VClass* compat_k = linker.FindClass(
                      &s, app_loader, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;");
                  VObject* compat = compat_k != nullptr ? linker.AllocObject(compat_k) : nullptr;
                  bool pm_ok = activity != nullptr && compat != nullptr;
                  if (pm_ok) {
                    Value cr{};
                    pm_ok = ExamInvoke(s, &linker, app_loader,
                                       "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;",
                                       "<init>", {}, "V", false, compat, nullptr, 0, &cr, &diag);
                  }
                  pm_ok = pm_ok &&
                      ExamInvoke(s, &linker, app_loader,
                                 "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;",
                                 "packageManagerFlow", {"Landroid/content/Context;"}, "I", false,
                                 compat, &pctx, 1, &pm, &diag);
                  ExamCheck c3;
                  c3.round = 6;
                  c3.name = "packageManagerFlow";
                  c3.ok = pm_ok && pm.i == 103;
                  c3.detail = pm_ok ? ("got " + std::to_string(pm.i)) : diag;
                  checks.push_back(std::move(c3));

                  Value sp{};
                  std::string diag2;
                  bool sp_ok =
                      activity != nullptr &&
                      ExamInvoke(s, &linker, app_loader, "Lcom/coke/target/RuntimeAndroidSpecials;",
                                 "probe", {"Landroid/content/Context;"},
                                 "Ljava/lang/String;", true, nullptr, &pctx, 1, &sp, &diag2);
                  std::string sp_text;
                  if (sp_ok && sp.ref != nullptr) {
                    sp_text =
                        static_cast<dexvm::runtime::VString*>(sp.ref)->ToModifiedUtf8();
                  }
                  ExamCheck c4;
                  c4.round = 6;
                  c4.name = "androidSpecials-bundle-intent";
                  c4.ok = sp_ok && sp_text.find("bundle=127") != std::string::npos &&
                          sp_text.find("intent=15") != std::string::npos;
                  c4.detail = sp_ok ? sp_text : diag2;
                  checks.push_back(std::move(c4));
                }
                if (round_on[7]) {
                  // R7 桥接出画：首帧标记 + envelope 结构（revision/字段）+ guest 文本
                  ExamCheck c1;
                  c1.round = 7;
                  c1.name = "first-frame-mark";
                  c1.ok = ui.first_frame_marked() && ui.view_tree_present_count() >= 1;
                  c1.detail = "marked=" + std::to_string(ui.first_frame_marked()) +
                              " presents=" + std::to_string(ui.view_tree_present_count());
                  checks.push_back(std::move(c1));

                  // 断言取首个全量快照（首帧基线；末条常为 patch，节点/文本在
                  // 基线里才完整——patch 是增量维护形）
                  const std::vector<uint8_t>& base_bytes = ui.first_full_tree().empty()
                                                               ? ui.last_view_tree()
                                                               : ui.first_full_tree();
                  dexvm::bridge::ViewSnapshot full;
                  dexvm::bridge::ViewPatch patch;
                  std::vector<dexvm::bridge::DrawCommand> draw;
                  uint32_t revision = 0;
                  bool is_patch = false;
                  bool parsed = !base_bytes.empty() &&
                                dexvm::bridge::ParseEnvelope(base_bytes, &full, &patch, &draw,
                                                             &revision, &is_patch);
                  ExamCheck c2;
                  c2.round = 7;
                  c2.name = "bridge-envelope";
                  c2.ok = parsed && revision >= 1 &&
                          (!full.nodes.empty() || !patch.ops.empty());
                  c2.detail = parsed
                                  ? "revision=" + std::to_string(revision) +
                                        (is_patch ? " patch" : " full") +
                                        " nodes=" + std::to_string(full.nodes.size()) +
                                        " patchOps=" + std::to_string(patch.ops.size()) +
                                        " drawCmds=" + std::to_string(draw.size()) +
                                        " presents=" + std::to_string(ui.view_tree_present_count())
                                  : "envelope parse failed";
                  checks.push_back(std::move(c2));

                  bool has_text_view = false;
                  std::string texts;
                  for (const auto& n : full.nodes) {
                    for (const auto& prop : n.props) {
                      if (prop.key == dexvm::bridge::kPropText && !prop.value.s.empty()) {
                        has_text_view = true;
                        // 扫描全量文本（资源布局树文本多，截断会把已知标记切掉）
                        texts += "[" + n.class_name + ":\"" + prop.value.s + "\"] ";
                      }
                    }
                  }
                  ExamCheck c3;
                  c3.round = 7;
                  c3.name = "bridge-guest-text";
                  // 便携首帧树（"target ok"）或直跑线树（panel/status 文本）均算出画
                  bool has_known_text =
                      base_bytes.empty()
                          ? false
                          : (texts.find("target ok") != std::string::npos ||
                             texts.find("Programmatic VM panel") != std::string::npos ||
                             texts.find("Android specials") != std::string::npos ||
                             texts.find("Guest") != std::string::npos ||
                             texts.find("UI cycle") != std::string::npos);
                  c3.ok = parsed && has_text_view && has_known_text;
                  c3.detail = "texts: " + texts;
                  checks.push_back(std::move(c3));
                }
              }

              ran_all = true;
              if (const dexvm::runtime::GuestSchedOps* sched = dexvm::runtime::GuestSched()) {
                sched->request_stop();
              }
              return true;
            });
          },
          &vm_crash_detail);
      if (!ok) {
        vm_crash = true;
        if (vm_crash_detail.empty()) {
          const char* reason = dexvm::runtime::GuestRunFailureReason();
          vm_crash_detail = std::string("guest execution failed (") +
                            (reason != nullptr && reason[0] != 0 ? reason
                                                                : "RunGuestMain returned false") +
                            ")";
        }
      }
      dexvm::runtime::GuestRuntimeReset(&state);
    }

    // ---- 逐轮汇总（表探针 + 专检；[Rn] 行 = tools 解析面）----
    int round_pass[9] = {};
    int round_total[9] = {};
    for (int i = 0; i < dexvm::tests::kTargetExamProbeCount; i++) {
      const dexvm::tests::TargetExamProbe& p = dexvm::tests::kTargetExamProbes[i];
      if (!round_on[p.round]) continue;
      // 表探针结果已逐条进 report；计数从 checks（表探针同形记录）来
    }
    for (const ExamCheck& c : checks) {
      if (c.round < 1 || c.round > 8) continue;
      round_total[c.round]++;
      if (c.ok) round_pass[c.round]++;
    }
    // 表探针（也进 checks）已含；dedup 不需要——表探针每条也 push 过 checks

    AppendFmt(&report, "\n== per-round verdicts ==\n");
    int rounds_green = 0;
    int rounds_run = 0;
    for (int r = 1; r <= 8; r++) {
      if (!round_on[r] || round_total[r] == 0) {
        if (round_on[r]) {
          AppendFmt(&report, "[R%d] SKIP no checks selected\n", r);
        }
        continue;
      }
      rounds_run++;
      bool green = round_pass[r] == round_total[r] && !vm_crash;
      if (green) rounds_green++;
      AppendFmt(&report, "[R%d] %s %d/%d (%s)\n", r, green ? "PASS" : "FAIL", round_pass[r],
                round_total[r], dexvm::tests::kRoundNames[r]);
    }

    AppendFmt(&report, "\n== round check details ==\n");
    for (const ExamCheck& c : checks) {
      AppendFmt(&report, "  R%d %s %s %s\n", c.round, c.ok ? "ok  " : "FAIL", c.name.c_str(),
                c.detail.c_str());
    }

    // R8 加固 .so 加载面记录（guest System.loadLibrary -> LibraryLoadHook）
    if (round_on[8]) {
      AppendFmt(&report, "\n-- R8 native loads (guest System.loadLibrary, %zu records) --\n",
                ctx.log.size());
      for (const LibLoadRecord& l : ctx.log) {
        AppendFmt(&report, "  %s -> %s %s %s\n", l.name.c_str(), l.path.c_str(),
                  l.ok ? "ok" : "FAIL", l.detail.c_str());
      }
      AppendFmt(&report, "apk native libs index: %s\n",
                so_index_ok ? "ok" : ("FAIL " + so_err).c_str());
    }

    AppendFmt(&report, "\nboot: %s (classes %zu)\n", runtime_dex.c_str(),
              linker.all_classes().size());
    AppendFmt(&report, "target: %s\n", target_apk.c_str());
    if (vm_crash) {
      AppendFmt(&report, "vm crash: %s\n", vm_crash_detail.c_str());
    }
    AppendFmt(&report, "rounds green: %d/%d\n", rounds_green, rounds_run);
    AppendFmt(&report, "RESULT: %s\n",
              (rounds_green == rounds_run && rounds_run > 0 && !vm_crash) ? "ALL GREEN"
                                                                         : "GAPS PRESENT");

    dexvm::runtime::SetLibraryLoadHook(nullptr);
    g_exam = nullptr;
  } while (false);

  if (boot_path != nullptr) env->functions->ReleaseStringUTFChars(env, runtime_dex_path, boot_path);
  if (app_path != nullptr) env->functions->ReleaseStringUTFChars(env, apk_path, app_path);
  if (rounds_c != nullptr) env->functions->ReleaseStringUTFChars(env, round_sel, rounds_c);
  return env->functions->NewStringUTF(env, report.c_str());
}

const JNINativeMethod kMethods[] = {
    {"nativeVmVersion", "()Ljava/lang/String;", reinterpret_cast<void*>(NativeVmVersion)},
    {"nativeGetRunProgress", "()Ljava/lang/String;",
     reinterpret_cast<void*>(NativeGetRunProgress)},
    {"nativeClearRunProgress", "()V", reinterpret_cast<void*>(NativeClearRunProgress)},
    {"nativeSetHostValues", "(Ljava/lang/String;)V",
     reinterpret_cast<void*>(NativeSetHostValues)},
    {"nativeRunFixture", "(Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(NativeRunFixture)},
    {"nativeRunApkLifecycle", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(NativeRunApkLifecycle)},
    {"nativeRunApkSession", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(NativeRunApkSession)},
    {"nativeSessionPollFrames", "()[B", reinterpret_cast<void*>(NativeSessionPollFrames)},
    {"nativeFontNextRequest", "()J", reinterpret_cast<void*>(NativeFontNextRequest)},
    {"nativeFontOp", "(J)I", reinterpret_cast<void*>(NativeFontOp)},
    {"nativeFontStyle", "(J)I", reinterpret_cast<void*>(NativeFontStyle)},
    {"nativeFontSize", "(J)F", reinterpret_cast<void*>(NativeFontSize)},
    {"nativeFontBase", "(J)J", reinterpret_cast<void*>(NativeFontBase)},
    {"nativeFontString", "(J)Ljava/lang/String;", reinterpret_cast<void*>(NativeFontString)},
    {"nativeFontText", "(J)Ljava/lang/String;", reinterpret_cast<void*>(NativeFontText)},
    {"nativeFontComplete", "(JJF[IZ)V", reinterpret_cast<void*>(NativeFontComplete)},
    {"nativeGetStartingWindow", "()[B",
     reinterpret_cast<void*>(NativeGetStartingWindow)},
    {"nativeSessionStop", "()V", reinterpret_cast<void*>(NativeSessionStop)},
    {"nativeSessionIsRunning", "()Z", reinterpret_cast<void*>(NativeSessionIsRunning)},
    {"nativeSetGuestTrace", "(Ljava/lang/String;)V",
     reinterpret_cast<void*>(NativeSetGuestTrace)},
    {"nativeSetGuestWindowSize", "(II)V",
     reinterpret_cast<void*>(NativeSetGuestWindowSize)},
    {"nativeTlsTake", "()[B", reinterpret_cast<void*>(NativeTlsTake)},
    {"nativeTlsReply", "(JIJ[B)V", reinterpret_cast<void*>(NativeTlsReply)},
    {"nativeSensorSetList", "(Ljava/lang/String;)V",
     reinterpret_cast<void*>(NativeSensorSetList)},
    {"nativeSensorPush", "(I[FJ)V", reinterpret_cast<void*>(NativeSensorPush)},
    {"nativeNotificationTake", "()[B", reinterpret_cast<void*>(NativeNotificationTake)},
    {"nativeToastTake", "()[B", reinterpret_cast<void*>(NativeToastTake)},
    {"nativeRunTlsSelfTest", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(NativeRunTlsSelfTest)},
    {"nativeRunExam", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(NativeRunExam)},
    {"nativeRunTargetExam",
     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(NativeRunTargetExam)},
};

}  // namespace

// 宿主点击 -> 常驻会话队列（ui_bridge_jni.cc 的 nativeDispatchViewClick 调用；
// 活会话内入队由会话线程派发 guest listener，避免跨线程触达 guest 堆）。
extern "C" __attribute__((visibility("default"))) bool DexVmSessionEnqueueClick(int32_t view_id) {
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  if (!g_session_state.active) return false;
  g_session_state.pending_click = view_id;
  return true;
}

// 按快照 handle.object_id 入队的点击（ui_bridge_jni.cc 调用）。object_id
// 是 guest View 身份，mId==0 的视图也能精确寻址。
extern "C" __attribute__((visibility("default"))) bool DexVmSessionEnqueueObjectClick(
    uint32_t object_id) {
  if (object_id == 0) return false;
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  if (!g_session_state.active) return false;
  g_session_state.pending_object_click = object_id;
  return true;
}

// 长按回灌入队（ui_bridge_jni.cc 调用）。
extern "C" __attribute__((visibility("default"))) bool DexVmSessionEnqueueLongClick(
    uint32_t object_id) {
  if (object_id == 0) return false;
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  if (!g_session_state.active) return false;
  g_session_state.pending_long_object = object_id;
  return true;
}

// 文本输入回灌入队（宿主 EditText 键入 -> guest setText/TextWatcher）。
extern "C" __attribute__((visibility("default"))) bool DexVmSessionEnqueueText(
    uint32_t object_id, const char* text, size_t len) {
  if (object_id == 0) return false;
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  if (!g_session_state.active) return false;
  g_session_state.pending_text_object = object_id;
  if (text != nullptr && len > 0) {
    g_session_state.pending_text.assign(text, len);
  } else {
    g_session_state.pending_text.clear();
  }
  return true;
}

// SeekBar 拖拽回灌入队（phase：0 progress / 1 start / 2 stop）。
extern "C" __attribute__((visibility("default"))) bool DexVmSessionEnqueueSeek(
    uint32_t object_id, int32_t progress, int32_t phase) {
  if (object_id == 0) return false;
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  if (!g_session_state.active) return false;
  g_session_state.pending_seek_object = object_id;
  g_session_state.pending_seek_progress = progress;
  g_session_state.pending_seek_phase = phase;
  return true;
}

// RatingBar 拖星回灌入队（milliRating = rating * 1000）。
extern "C" __attribute__((visibility("default"))) bool DexVmSessionEnqueueRating(
    uint32_t object_id, int32_t milli_rating) {
  if (object_id == 0) return false;
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  if (!g_session_state.active) return false;
  g_session_state.pending_rating_object = object_id;
  g_session_state.pending_rating_milli = milli_rating;
  return true;
}

// DrawerLayout 开合回灌入队（open：1 开 / 0 合）。
extern "C" __attribute__((visibility("default"))) bool DexVmSessionEnqueueDrawer(
    uint32_t object_id, int32_t open) {
  if (object_id == 0) return false;
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  if (!g_session_state.active) return false;
  g_session_state.pending_drawer_object = object_id;
  g_session_state.pending_drawer_open = open;
  return true;
}

// Toolbar 菜单项点击回灌入队（index = 菜单项序号）。
extern "C" __attribute__((visibility("default"))) bool DexVmSessionEnqueueMenuClick(
    uint32_t object_id, int32_t index) {
  if (object_id == 0) return false;
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  if (!g_session_state.active) return false;
  g_session_state.pending_menu_object = object_id;
  g_session_state.pending_menu_index = index;
  return true;
}

// DatePicker/TimePicker 字段回灌入队（a,b,c = 年月日 / 时分）。
extern "C" __attribute__((visibility("default"))) bool DexVmSessionEnqueueFields(
    uint32_t object_id, int32_t a, int32_t b, int32_t c) {
  if (object_id == 0) return false;
  std::lock_guard<std::mutex> lk(g_session_state.mu);
  if (!g_session_state.active) return false;
  g_session_state.pending_fields_object = object_id;
  g_session_state.pending_fields_a = a;
  g_session_state.pending_fields_b = b;
  g_session_state.pending_fields_c = c;
  return true;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->functions->GetEnv(vm, reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }
  jclass clazz = env->functions->FindClass(env, kClassName);
  if (clazz == nullptr) {
    return JNI_ERR;
  }
  if (env->functions->RegisterNatives(env, clazz, kMethods, std::size(kMethods)) != 0) {
    return JNI_ERR;
  }
  return JNI_VERSION_1_6;
}
