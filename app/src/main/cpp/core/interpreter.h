// 解释器（语义对照 third_party/art/runtime/interpreter/interpreter_common.cc）
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/class_linker.h"
#include "core/vm_types.h"

namespace dexvm::runtime {

// 帧寄存器存储：绝大多数方法 ≤31 个 vreg（64 个 32-bit payload 槽），
// 内联承接、不再每次调用 malloc/free（ART 用连续托管栈，这里做等价最小面）。
// 超过内联容量的方法才走堆（大方法体少，启动期占比低）。
class FrameRegs {
 public:
  FrameRegs() = default;
  FrameRegs(const FrameRegs&) = delete;
  FrameRegs& operator=(const FrameRegs&) = delete;
  ~FrameRegs() {
    if (data_ != InlineData()) ::operator delete(data_);
  }

  void assign(size_t n, uint32_t v) {
    Ensure(n);
    for (size_t i = 0; i < n; i++) data_[i] = v;
    size_ = n;
  }

  void resize(size_t n, uint32_t v) {
    Ensure(n);
    for (size_t i = size_; i < n; i++) data_[i] = v;
    size_ = n;
  }

  size_t size() const { return size_; }
  uint32_t& operator[](size_t i) { return data_[i]; }
  const uint32_t& operator[](size_t i) const { return data_[i]; }
  uint32_t* data() { return data_; }
  const uint32_t* data() const { return data_; }

 private:
  static constexpr size_t kInlineSlots = 64;

  uint32_t* InlineData() { return reinterpret_cast<uint32_t*>(inline_storage_); }

  void Ensure(size_t n) {
    if (n <= cap_) return;
    uint32_t* p = static_cast<uint32_t*>(::operator new(n * sizeof(uint32_t)));
    if (size_ != 0) std::memcpy(p, data_, size_ * sizeof(uint32_t));
    if (data_ != InlineData()) ::operator delete(data_);
    data_ = p;
    cap_ = n;
  }

  alignas(uint32_t) unsigned char inline_storage_[kInlineSlots * sizeof(uint32_t)];
  uint32_t* data_ = InlineData();
  size_t size_ = 0;
  size_t cap_ = kInlineSlots;
};

// 解释器帧：vreg 用 32-bit 槽（wide 占两槽）
struct Frame {
  VMethod* method = nullptr;
  const uint16_t* insns = nullptr;
  uint32_t dex_pc = 0;  // code-unit 偏移
  FrameRegs regs;
  Frame* caller = nullptr;
  // 所属 GuestTask（frame_stack 多任务共享；回溯按 owner 过滤，防止把
  // 其他协程的帧串进异常栈；不跟 caller 链——caller 可能跨 task/已回收）。
  void* owner = nullptr;
};

// VM 全局状态（M1 单执行流；协程接入后按 GuestTask 分离）
struct VMState {
  mem::Heap* heap = nullptr;
  InternTable* intern = nullptr;
  ClassLinker* linker = nullptr;
  ObjHeader* pending_exception = nullptr;
  std::vector<Frame*> frame_stack;
  // 最近一次 Throw/ThrowNew 的 guest 栈快照（innermost 先；fillInStackTrace
  // 语义的诊断面）。ClearException 不清（供失败诊断读取）；Throw 时整体覆盖。
  std::vector<std::string> last_throw_backtrace;

  void ThrowNew(std::string_view descriptor, std::string_view msg);
  void Throw(ObjHeader* exception);
  bool HasPendingException() const { return pending_exception != nullptr; }
  void ClearException() { pending_exception = nullptr; }
};

// 执行一个方法（已选好实现）；args 按 jvalue 语义（wide 一个 Value 装 64 位）
// 成功返回 true 且 *result 填返回值；失败（抛异常）返回 false，
// 异常挂在 state.pending_exception。
// 启动期慢方法采样（设备诊断）：记录当前执行的 guest 方法。
void SetGuestMethodProfile(bool on);
bool GuestMethodProfileEnabled();
VMethod* CurrentGuestMethod();

bool InvokeMethod(VMState& state, VMethod* method, const Value* args, size_t arg_count,
                  Value* result);

// 类定义完整性防御（M4.3）：InitializeClass 前校验——定义未完成的残留类
// （DefineClassImpl 中 super/interface 解析失败：停在 kLoaded、LinkClass 未跑、
// static_storage 未建）进入 <clinit> 静态值解码会写空槽崩（SIGSEGV 根因）。
// 残留类按 Java 语义抛 NoClassDefFoundError；健康类返回 true。
bool CheckClassDefinition(VMState& state, VClass* klass);

// 帧内执行（供 InvokeMethod 与异常 unwind 使用）
bool ExecuteFrame(VMState& state, Frame& frame);

// JNI native 派发钩子（jni 模块注册，避免 core↔jni 静态库循环依赖）：
// jni_native_fn 非空的方法经此钩子走 jni::InvokeNativeTrampoline。
using NativeCallHook = bool (*)(VMState*, VMethod*, const Value*, size_t, Value*);
void SetNativeCallHook(NativeCallHook hook);
NativeCallHook GetNativeCallHook();

// 调试面：guest 方法入口 trace（类 descriptor 子串匹配，逗号分隔；空串=关闭）。
// 仅设备诊断用；开启后每条命中方法一行 logcat（tag=DexVmTrace），默认关闭。
void SetGuestMethodTrace(std::string patterns);

}  // namespace dexvm::runtime
