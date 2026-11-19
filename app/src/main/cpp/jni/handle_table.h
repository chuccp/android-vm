// JNI 间接引用表（docs 05 章 15.3/15.4/15.15）
// 语义对照：ART IndirectReferenceTable / LocalReferenceTable / JavaVMExt globals。
// jobject 是不透明句柄（index+generation 编码，非裸指针）：
//   Decode/Encode 只经本表（docs 15.4 DecodeJObject/EncodeJObject）。
#pragma once

#include <cstdint>
#include <vector>

namespace dexvm::runtime {
struct ObjHeader;
}

namespace dexvm::jni {

// ---- 句柄编码 ----
// 32-bit IndirectRef 语义（可承载于指针位型的 jobject）：
//   bit 31-30  kind        0=local / 1=global / 2=weak global / 3=reserved
//   bit 29-20  generation  槽复用递增计数（过期句柄检出，docs 15.4
//                          "validate entry generation"）
//   bit 19-0   index+1     0 = null 句柄（JNI NULL）
// 代位 6->10 扩容（回绕 63->1023 代）+ 回绕即退役（retire，失败语义化）：
// 解密循环 local ref churn 巨大时旧 6 代位 63 代回绕 = 过期句柄误判为活跃
// 对象（悬挂 jobject 解引用 / 类型混淆崩溃）；退役后该槽永不再复用，
// 过期句柄恒检出（代位与退役双保险）。
using Handle = uint32_t;

inline constexpr Handle kNullHandle = 0;
inline constexpr uint32_t kGenerationBits = 10;
inline constexpr uint32_t kIndexBits = 20;
inline constexpr uint32_t kGenerationShift = kIndexBits;  // bit 29-20
inline constexpr uint32_t kMaxHandleSlots = (1u << kIndexBits) - 1;
inline constexpr uint32_t kGenerationMask = (1u << kGenerationBits) - 1;
// 槽体超高水位（kMaxHandleSlots 的 7/8）后才允许复用已释放槽（代位递增，
// 陈旧解码仍恒失败）：常态分配单调新槽（真 ART 形态），高水位只防
// 长跑进程累计槽量打满表体（1M 槽 ≈ 32MB 上限）后的硬 OOM。
inline constexpr uint32_t kSlotReuseHighWater = (kMaxHandleSlots / 8u) * 7u;
// local 表容量（ART kMaxLocalReferenceCapacity 同量级）：EnsureLocalCapacity
// 按此核算，live 计数超限时 Add 返回 kNullHandle（耗尽语义：null + OOM 抛出，
// jni_env EncodeObj 落点，不误判不静默）
inline constexpr uint32_t kMaxLocalRefs = 512;

enum class RefKind : uint8_t {
  kLocal = 0,
  kGlobal = 1,
  kWeakGlobal = 2,
  kInvalid = 3,
};

Handle MakeHandle(RefKind kind, uint32_t generation, uint32_t index);
RefKind GetHandleKind(Handle h);              // 位型非法 -> kInvalid
bool DecodeHandle(Handle h, uint32_t* index, uint32_t* generation);  // null/非法 -> false

// ---- Local 引用表：每 JNIEnv 一张，frame 栈分层 ----
// docs 15.12/15.15：native 调用帧进入/返回自动释放；
// PushLocalFrame/PopLocalFrame 显式分层；base frame 随 Env 存续。
//
// 槽分配（真 ART IndirectRef 形态 + 陈旧句柄恒检出）：每次 Add 拿【新槽】
// （slot 号单调递增）——
//   1) 对象返回句柄恒为"本次帧新鲜 local ref"（oracle 终审对照：真 ART 每笔
//      新 slot 单调，VM 旧 free-list 复用 = 早期 slot 陈旧句柄）；
//   2) 释放槽只按代位退役或（表体超高水位后的）代位递增复用——任何跨帧
//      缓存/复用的旧句柄在解码端必然失败（live+generation 双校验 + 代位
//      回绕即退役，过期句柄不再有"代位碰撞解到错对象"的窗口）。
// 容量核算（kMaxLocalRefs）按 live 计数不变（frame 出栈即 free）。
class LocalRefTable {
 public:
  LocalRefTable();

  void PushFrame();   // PushLocalFrame
  bool PopFrame();    // PopLocalFrame：释放当前 frame 全部句柄；base 不可弹出
  size_t frame_depth() const { return frames_.size(); }

  Handle Add(runtime::ObjHeader* obj);      // EncodeJObject：新槽（单调）加入当前 frame
  runtime::ObjHeader* Get(Handle h) const;  // DecodeJObject：kind+live+generation 校验
  // DeleteLocalRef：仅当前 frame 的 entry 生效；属于其他 frame 的 entry
  // no-op 返回 false（ART：JNI WARNING + dump 栈，不抛异常，docs 15.15）。
  bool Remove(Handle h);
  bool Contains(Handle h) const;            // GetObjectRefType 校验
  // 诊断面（陈旧句柄归因）：槽状态（index/generation/live/retired/frame_depth）。
  std::string DescribeHandle(Handle h) const;

  // EnsureLocalCapacity 真实容量核算（docs 15.12）：
  // 可再建 capacity 个 local 引用（live < kMaxLocalRefs）返回 true。
  bool EnsureCapacity(int32_t capacity) const;
  size_t live_count() const { return live_; }
  size_t remaining() const {
    return live_ >= kMaxLocalRefs ? 0 : kMaxLocalRefs - live_;
  }

 private:
  struct Slot {
    uint32_t generation = 0;
    runtime::ObjHeader* obj = nullptr;
    bool live = false;
    bool retired = false;  // 代位回绕即退役：永不再复用（过期句柄恒检出）
    uint32_t frame_depth = 0;
  };
  void FreeSlot(uint32_t index);

  std::vector<Slot> slots_;
  std::vector<uint32_t> free_;  // 高水位复用池（代位递增；常态不用）
  size_t live_ = 0;
  struct FrameRec {
    std::vector<uint32_t> owned;
  };
  std::vector<FrameRec> frames_;  // frames_[0] = base（native 调用基础帧）
};

// ---- Global / WeakGlobal 表：挂 JavaVM（docs 15.15）----
// 槽分配同 LocalRefTable：新槽单调 + 释放即退役（陈旧句柄恒检出）。
class GlobalRefTable {
 public:
  Handle Add(runtime::ObjHeader* obj);      // NewGlobalRef（强根）
  Handle AddWeak(runtime::ObjHeader* obj);  // NewWeakGlobalRef（GC 可清除）
  runtime::ObjHeader* Get(Handle h) const;  // weak 已清除 -> nullptr
  bool Remove(Handle h);                    // Delete(Global|WeakGlobal)Ref
  bool Contains(Handle h) const;
  RefKind Classify(Handle h) const;         // kGlobal / kWeakGlobal / kInvalid

  // weak clear（GC 语义：docs 15.15 "GC may clear entry"；M1 由模拟 GC 调用，
  // 清除后 NewLocalRef(weak) -> null、IsSameObject(weak, null) -> true）
  bool ClearWeak(Handle h);

 private:
  struct Slot {
    uint32_t generation = 0;
    runtime::ObjHeader* obj = nullptr;
    bool live = false;
    bool retired = false;  // 释放/代位回绕即退役：永不再复用（过期句柄恒检出）
    bool weak = false;
  };
  std::vector<Slot> slots_;
};

}  // namespace dexvm::jni
