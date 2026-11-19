// VM 内置类工厂（java.lang.Object/String/Class/Throwable 骨架 + 常用异常类
// + M3 jre-excluded 内建面：System/Runtime/Enum/Thread/ThreadGroup、
// java.lang.reflect 全套、dalvik.system.VMRuntime hidden API 面）
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dexvm::runtime {

class ClassLinker;
struct VMState;
struct VClass;
struct ObjHeader;

// 登记内置类骨架到 ClassLinker（RegisterBuiltinClass；AllocString 挂 String 类；
// 异常对象经 ThrowNew 挂 Throwable 子类）。幂等：重复调用无副作用。
void RegisterBuiltins(ClassLinker* linker);

// ---- 单一事实源：core/builtin_classes.def（X 宏清单）----
// 内建类集（描述符）由 builtin_classes.def 权威表达：C++ 注册面（本文件/
// framework_builtins.cc）与构建派生（runtime-libs 打包排除集）、校验工具
// （tools/runtime_coverage_tests.sh）同源读取——废除 allowlist/excluded 双表单。
const std::vector<std::string>& CoreBuiltinClassManifest();      // core/builtins.cc 注册集
const std::vector<std::string>& FrameworkBuiltinClassManifest();  // framework 注册集
const std::vector<std::string>& BuiltinClassManifest();           // 并集（全量内建）

// 「内建注册集 == 派生集」一致性断言（防再犯核心）：registered（本次
// RegisterBuiltinClass 实际登记名集）与 manifest（builtin_classes.def 派生集）
// 双向比对；不一致时 error 填差异清单并返回 false。
bool VerifyBuiltinClassSet(const std::vector<std::string>& registered,
                           const std::vector<std::string>& manifest, std::string* error);

// 注册收尾断言：VClass* 登记集与清单双向一致；不一致打印差异并 abort
// （内建面漂移 = 构建/启动即失败，禁止双实现再犯）。
void AssertBuiltinRegistration(const std::vector<VClass*>& all,
                               const std::vector<std::string>& manifest, const char* registrar);

// ---- <clinit> 失败 erroneous 语义（docs 03 章 12，Android 行为）----
// <clinit> 抛异常的类进入 erroneous 状态：后续 active use 抛
// NoClassDefFoundError，【不再重跑 <clinit>】。ClassLinker::InitializeClass
// 的失败路径把状态退回 kResolved（重试友好），若不在调用侧收口，失败的
// <clinit> 会被反复重跑（每次重跑 = 静态字段字面量解密清单重新走一遍 =
// "循环不推进"原地打转实证）。登记表挂 VClass* 为键，只增不清。
void NoteClinitFailure(VClass* klass);
bool IsClinitFailed(const VClass* klass);

// java.lang.* 标准 native 方法（Float/Double 位转换等）绑定到 VM intrinsic。
// 类加载（DefineClass）后调用：给 vendored Float/Double 等 lib 类的 native 方法
// 槽挂 intrinsic 实现（否则 native 无实现 -> UnsatisfiedLinkError）。通用登记，
// 非考卷特判；幂等（已绑/有码/有 JNI 者跳过）。
void BindWellKnownNatives(VClass* klass);

// ---- 宿主 native 库加载钩子（System.loadLibrary/Runtime.loadLibrary 语义落点，
// docs 07 章 16.2）：按名加载 guest native 库并执行 JNI_OnLoad 由宿主侧实现
// （jni NativeManager 或宿主测试 loader）。未安装钩子时 loadLibrary 抛
// UnsatisfiedLinkError（无加载器语义，不伪造成功）。----
using LibraryLoadHook = bool (*)(const std::string& library_name, std::string* error);
void SetLibraryLoadHook(LibraryLoadHook hook);
LibraryLoadHook GetLibraryLoadHook();

// ---- 消息泵钩子（M4.3，docs 14.10 单线程投影）：Thread.sleep 即泵点——
// 休眠前泵出已投递的 Looper 消息（postDelayed 虚拟时间推进；保证
// "postDelayed(Runnable, 0/小延迟) 必须能被执行到"）。由
// core/framework_builtins 安装（PumpLooperQueues 落点）；未安装时 sleep
// 仅真实休眠。返回 false = 泵出的 guest 异常（错误经 error 上报，与
// loadLibrary 钩子同约定）。----
using MessagePumpHook = bool (*)(VMState& state, std::string* error);
void SetMessagePumpHook(MessagePumpHook hook);

// ---- M4.4 Guest 线程运行时（docs 04 章 14.2/14.8：Thread.start/join/interrupt、
// Object.wait/notify/notifyAll、Unsafe.park/unpark 的协程接线）----
//
// scheduler 接缝：VM 侧 Thread/Object/Unsafe 内建经本结构调用协程调度
// （coroutine/ GuestTask/调度接线），保持既有宿主测试脚本零改——不链
// coroutine/ 的二进制不装接线 = 旧单流退化（RunGuestMain 直通）。
// 实现见 coroutine/scheduler.cc（InstallVmSchedBackend）。

// wait 种类（与 coroutine WaitState 对齐的稳定值；接线侧映射）
enum GuestWaitKind {
  kGuestWaitMutex = 1,      // monitor 争用（mutex waiter list）
  kGuestWaitMonitor = 2,    // Object.wait（object monitor waiter list）
  kGuestWaitCondition = 3,  // park/unpark（condition wait queue）
  kGuestWaitDeadline = 4,   // sleep/deadline（TimerHeap）
  kGuestWaitJoin = 5,       // Thread.join（task completion wait channel）
};

struct GuestSchedOps {
  // 建调度器并以 main GuestTask（entry(task_handle, arg)）跑至 RequestStop /
  // 全部完成 / 超时（ms）。entry 在 main task 上下文执行（阻塞原语可用）。
  bool (*run_main)(void (*entry)(void*, void*), void* arg, unsigned timeout_ms);
  // 创建 GuestTask（entry(task_handle, arg)）；stack_bytes=0 取默认 fiber 栈。
  void* (*spawn)(void (*entry)(void*, void*), void* arg, unsigned stack_bytes);
  // 协程 wait 至 pred(arg) 成立或 deadline_ms 到期（now_ms() 约定的绝对单调
  // 时刻；<0 = 无期限）。返回 true = 谓词成立；false = deadline 到期。
  // 仅可在 guest task 上下文调用。
  bool (*wait)(int kind, long long deadline_ms, bool (*pred)(void*), void* arg);
  // 唤醒 task：kind == 0 = 任意等待（interrupt/unpark），否则同 kind 匹配唤醒。
  void (*wake)(void* task, int kind);
  // 让出当前 guest task（回边/Thread.yield 落点）；返回是否实际让出。
  bool (*yield_now)();
  bool (*has_ready)();     // 有其他可运行 task（让出压力判据）
  void (*request_stop)();  // 请求结束 run_main 循环（guest 进程退出策略）
  void* (*current_task)();  // 当前 guest task 句柄（非 task 上下文为 null）
  long long (*now_ms)();    // 单调时钟毫秒
  // GC 保守栈扫描接缝（docs 02 章 6.9）：枚举挂起 GuestTask 的 fiber 栈 +
  // ucontext 寄存器保存区（add(base, bytes, arg) 登记一段）。由
  // SetGuestSchedOps 装接时转发进 memory/heap.cc 栈区枚举口（gc.cc 消费）；
  // 未装接线（nullptr）= 仅扫本线程 C++ 栈。
  void (*visit_stacks)(void (*add)(void* base, size_t bytes, void* arg), void* arg);
  // 当前 guest task 的 fiber 栈边界 [low, high)（low = guard 页下沿；high = 栈顶）。
  // 解释器 C++ 栈护栏（StackOverflowError 预检）消费；非 task 上下文/未装 = false。
  bool (*current_stack_bounds)(uintptr_t* low, uintptr_t* high);
};

// VM guest 解释执行 fiber 栈口径（递归解释器 C++ 栈；Thread.start 与 main
// GuestTask 同口径——OpsRunMain 与 spawn 共用本常量）。
inline constexpr unsigned kGuestStackBytes = 4u * 1024 * 1024;

// 接线表（coroutine/scheduler.cc 实现）：消费方自行
// SetGuestSchedOps(VmSchedBackendOps()) 装载（thread_wait 冒烟 / 设备考）。
const GuestSchedOps* VmSchedBackendOps();
const GuestSchedOps* GuestSched();        // 未装载 = nullptr（旧单流）
void SetGuestSchedOps(const GuestSchedOps* ops);
// guest task 结束回调（JNI local 引用表等按 task 资源回收接缝；nullptr = 无）。
void SetGuestTaskEndHook(void (*fn)(void* task));
void NotifyGuestTaskEnd(void* task);

// 主流程驱动：以 main GuestTask 执行 fn（Thread.start/join、Object.wait 的
// 协程 wait/wake 要求 guest 执行处于 guest task 上下文）；未装载接线时直通
// fn（旧单流，既有宿主测试行为不变）。
using GuestMainFn = bool (*)(VMState& state, void* arg);
bool RunGuestMainImpl(VMState& state, GuestMainFn fn, void* arg);
template <typename Fn>
bool RunGuestMain(VMState& state, Fn fn) {
  return RunGuestMainImpl(
      state, [](VMState& s, void* arg) -> bool { return (*static_cast<Fn*>(arg))(s); }, &fn);
}

// guest 全程预算（RunGuestMain 的调度器 deadline；默认 60s 救死锁）。
// 设备考业务链可深跑数分钟：按需放大（runtime_contract 设 30min），预算中止
// 归 GuestRunFailureReason 诊断，不报 "unknown signal"。
void SetGuestRunBudgetMs(long long ms);
long long GuestRunBudgetMs();
// RunGuestMain 失败归因（无信号失败：预算耗尽/调度器中止/未进入）；成功/无
// 失败时为空串。诊断面（"unknown signal" 误报修正）。
const char* GuestRunFailureReason();

// monitor 进/出（core/interpreter.cc kMonitorEnter/kMonitorExit 落点）：
// ObjHeader.monitor 保持重入计数；owner/等待队列在 guest 线程运行时侧表。
// Enter 可争用协程挂起；Exit 返回 false = 已挂 IllegalMonitorStateException。
void MonitorEnterImpl(VMState& state, ObjHeader* obj);
bool MonitorExitImpl(VMState& state, ObjHeader* obj);

// 回边让出点（解释器后向分支；多 GuestTask 并发推进。单任务/旧单流直返）。
void GuestYieldPoint();

// ---- 分配失败终点 OOM 类语义（docs 12 章 22.10 AllocateInternalWithGc）----
// 重试链（Heap::Alloc）耗尽后抛真 java.lang.OutOfMemoryError（可 catch 语义）：
// 堆有余量走 ThrowNew 正常路径；堆耗尽时 ThrowNew 落裸兜底（klass==null）——
// 换预分配单例（ART pre-allocated OutOfMemoryError：堆未满时预建 + Heap::Pin
// 钉住根，GC 恒保活）。
// EnsureOomReserve：引导点预建（堆耗尽后无法再建）；幂等。
// ThrowOOM：分配失败终点抛出（解释器/内建的 OOM 抛点统一走此口）。
ObjHeader* EnsureOomReserve(VMState& state);
void ThrowOOM(VMState& state, std::string_view msg);

// 裸兜底异常升级（分配失败归因，docs 12 章 22.10）：pending 为裸对象
// （klass==null）且堆近满（used≈capacity）= 兜底因堆耗尽——换预分配真
// OutOfMemoryError（覆盖不可改抛点：framework_builtins.cc 等经 ThrowNew 的
// 穷尽路径）。类缺失归因（堆有余量）不升级，保留裸对象诊断。
// 解释器 unwind 入口（core/interpreter.cc）调用；幂等。
void UpgradeBareOomIfExhausted(VMState& state);

// guest 线程运行时复位（vm-crash 后 guest task 句柄失效，防再入踩悬挂指针；
// state 非空时同清其残留活跃帧/异常——被终止 task 的帧是悬挂指针）。
void GuestRuntimeReset(VMState* state = nullptr);

}  // namespace dexvm::runtime
