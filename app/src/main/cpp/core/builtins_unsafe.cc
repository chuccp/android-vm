// jdk.internal.misc.Unsafe 全族 + java.util.concurrent.atomic 原子面。
// 纯移动自 core/builtins.cc（零行为变更；注册/绑定仍在 registrar）。

#include "core/builtins.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <set>
#include <string>
#include <time.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include <dirent.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <arpa/inet.h>

#include "core/exception.h"

#include "core/class_linker.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "session/class_loader.h"
#include "session/session.h"


#include "core/builtins_internal.h"

namespace dexvm::runtime {

// ==== M4.2 知乎考卷：runtime.dex 类覆盖的 VM 内建依赖面 =====================
// 零壳适配：jre-excluded/VM 职责类按语义建面（AtomicInteger 的 Unsafe 原子面、
// ThreadLocal 的 Thread.threadLocals/WeakReference 链、Robust 的 ThreadLocal
// instanceof 面）。不识别壳、不伪造业务返回值。

// ---- jdk.internal.misc.Unsafe（int 槽面：8 字节槽 + 低 4 字节 kWord 窄化）----
//
// 静态槽寻址契约（staticFieldBase/Offset 配对）：staticFieldOffset 返回
// kStaticSlotTag | 槽绝对地址 的带标记偏移——偏移自带槽位（kStaticSlotTag
// 高位标记 + static_storage 槽指针），base 可给 declaring Class 亦可为
// null（sun.misc.Unsafe 静态槽直取语义：null base + 带标偏移 = 该静态槽）。
// UnsafeSlot 按标记分流：带标偏移直取槽地址（忽略 base），否则对象槽
// （field_data() 基准）。tag 不与合法对象偏移/用户态指针冲突（对象偏移远
// 小于 2^62；用户态指针最高位区不置 bit 62）。
namespace {
constexpr uint64_t kStaticSlotTag = 0x4000000000000000ull;

void* UnsafeSlot(VMState& state, const Value* args, size_t n, bool* ok) {
  if (n < 2) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "Unsafe slot args");
    *ok = false;
    return nullptr;
  }
  uint64_t off = static_cast<uint64_t>(args[1].l);
  if ((off & kStaticSlotTag) != 0) {
    // 静态槽：偏移自带槽地址（见寻址契约）；base（declaring Class 或
    // null）不参与寻址。
    return reinterpret_cast<void*>(off & ~kStaticSlotTag);
  }
  if (args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "Unsafe target");
    *ok = false;
    return nullptr;
  }
  return static_cast<VObject*>(args[0].ref)->field_data() + off;
}

int32_t UnsafeLoadInt(void* slot) {
  uint64_t v = 0;
  std::memcpy(&v, slot, sizeof(v));
  return static_cast<int32_t>(static_cast<uint32_t>(v));
}

void UnsafeStoreInt(void* slot, int32_t v) {
  uint64_t z = static_cast<uint64_t>(static_cast<uint32_t>(v));  // kWord 写：高位清零
  std::memcpy(slot, &z, sizeof(z));
}

// ---- 数组面：JVM 逻辑布局（packed）<-> VM 8 字节元素槽 ----
//
// HotSpot 语义（JDKUtils/IOUtils 等 OpenJDK 源码依赖）：数组偏移 =
// arrayBaseOffset() + index * arrayIndexScale()，且标量访问按【逻辑 packed
// 布局】的字节偏移解释：char[]/short[] 元素 2 字节、int[]/float[] 4 字节、
// long[]/double[]/引用 8 字节、byte[]/boolean[] 1 字节。访问可跨元素
// （如 Unsafe.getLong(char[], base + 2*i) 读 4 个 char）。
//
// 本 VM 的元素存储仍是 8 字节/槽（解释器 aget/aput 契约），因此这里做
// 逻辑字节偏移 -> 元素槽地址的转换：字节 b 落在元素 b/elem_size 的
// b%elem_size 字节（VM 窄化写入把值放在槽低字节，小端）。
// 未做此转换时 fastjson2 2.0.66 的 getLongLE(char[]) 会读到错误 word，
// 字符串扫描跳过闭合引号（R5 fastJson2Flow 的根因）。
constexpr int64_t kArrayLogicalBase = static_cast<int64_t>(sizeof(VArray) - sizeof(ObjHeader));
constexpr int64_t kArraySlotBytes = 8;

int32_t UnsafeArrayElemSize(const VClass* component) {
  if (component != nullptr && component->is_primitive) {
    switch (component->primitive_type) {
      case 'Z':
      case 'B':
        return 1;
      case 'C':
      case 'S':
        return 2;
      case 'I':
      case 'F':
        return 4;
      case 'J':
      case 'D':
        return 8;
      default:
        break;
    }
  }
  return 8;  // 引用数组：VM 槽即指针宽度
}

struct UnsafeArrayRef {
  VArray* arr = nullptr;
  int32_t elem_size = 8;
  bool valid = false;
};

UnsafeArrayRef UnsafeArrayOf(const Value* args, size_t n) {
  UnsafeArrayRef out;
  if (n < 2 || args[0].ref == nullptr || args[0].ref->klass == nullptr ||
      !args[0].ref->klass->is_array) {
    return out;
  }
  out.arr = reinterpret_cast<VArray*>(args[0].ref);
  out.elem_size = UnsafeArrayElemSize(args[0].ref->klass->component_type);
  out.valid = true;
  return out;
}

bool UnsafeArrayRange(VMState& state, const UnsafeArrayRef& a, int64_t byte_off, size_t width,
                      bool* ok) {
  int64_t bytes = static_cast<int64_t>(a.arr->length) * a.elem_size;
  if (byte_off < kArrayLogicalBase || byte_off > bytes ||
      static_cast<int64_t>(width) > bytes - byte_off) {
    state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;",
                   "Unsafe array access out of bounds");
    *ok = false;
    return false;
  }
  return true;
}

uint64_t UnsafeArrayRead(const UnsafeArrayRef& a, int64_t byte_off, size_t width) {
  const uint8_t* slots = a.arr->data();
  uint64_t v = 0;
  for (size_t j = 0; j < width; ++j) {
    int64_t b = byte_off - kArrayLogicalBase + static_cast<int64_t>(j);
    int64_t idx = b / a.elem_size;
    int64_t within = b - idx * a.elem_size;
    v |= static_cast<uint64_t>(slots[idx * kArraySlotBytes + within]) << (8 * j);
  }
  return v;
}

void UnsafeArrayWrite(const UnsafeArrayRef& a, int64_t byte_off, uint64_t v, size_t width) {
  uint8_t* slots = a.arr->data();
  for (size_t j = 0; j < width; ++j) {
    int64_t b = byte_off - kArrayLogicalBase + static_cast<int64_t>(j);
    int64_t idx = b / a.elem_size;
    int64_t within = b - idx * a.elem_size;
    slots[idx * kArraySlotBytes + within] = static_cast<uint8_t>(v >> (8 * j));
  }
}

}  // namespace
Value UnsafeGetUnsafe(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  VClass* k = state.linker->FindClass("Ljdk/internal/misc/Unsafe;");
  if (k == nullptr || k->static_storage.empty()) {
    state.ThrowNew("Ljava/lang/InternalError;", "Unsafe missing");
    *ok = false;
    return Value{};
  }
  if (k->static_storage[0].ref == nullptr) {
    VObject* u = state.linker->AllocObject(k);
    if (u == nullptr) {
      ThrowOOM(state, "Unsafe.getUnsafe");
      *ok = false;
      return Value{};
    }
    k->static_storage[0].ref = u;
  }
  Value r{};
  r.ref = k->static_storage[0].ref;
  return r;
}

Value UnsafeObjectFieldOffset(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)self;
  if (n < 2 || args[0].ref == nullptr || args[1].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "objectFieldOffset");
    *ok = false;
    return Value{};
  }
  VClass* k = reinterpret_cast<VClass*>(args[0].ref);
  std::string name = Mutf8Arg(args[1]);
  VField* f = FindInstField(k, name.c_str());
  if (f == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "no such field: " + name);
    *ok = false;
    return Value{};
  }
  Value r{};
  r.l = static_cast<int64_t>(f->offset);
  return r;
}

// objectFieldOffset(Field) 形（java.util.Random.<clinit> seed 偏移面;zonfig
// UUID$Holder -> SecureRandom 链缺口）。语义同 (Class,String) 形:返回实例字段
// 槽偏移,与 UnsafeSlot 寻址契约一致。
Value UnsafeObjectFieldOffsetF(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "objectFieldOffset(Field)");
    *ok = false;
    return Value{};
  }
  VField* f = MirrorField(static_cast<VObject*>(args[0].ref));
  if (f == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "objectFieldOffset: null field mirror");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.l = static_cast<int64_t>(f->offset);
  return r;
}

// 通用位读取：数组面按逻辑 packed 偏移（可跨元素）组装；对象/静态槽按 8 字节槽
// 读取（旧语义）。
bool UnsafeReadBits(VMState& state, const Value* args, size_t n, size_t width, uint64_t* out,
                    bool* ok) {
  UnsafeArrayRef a = UnsafeArrayOf(args, n);
  if (a.valid) {
    int64_t off = args[1].l;
    if (!UnsafeArrayRange(state, a, off, width, ok)) return false;
    *out = UnsafeArrayRead(a, off, width);
    return true;
  }
  void* slot = UnsafeSlot(state, args, n, ok);
  if (!*ok) return false;
  *out = 0;
  std::memcpy(out, slot, width);
  return true;
}

// 窄化写入：非数组保持原 8 字节槽写法（slot_image 已按 NarrowWrite 编码）；
// 数组面写 packed 布局的 width 字节。
bool UnsafeWriteBits(VMState& state, const Value* args, size_t n, uint64_t slot_image,
                     size_t width, bool* ok) {
  UnsafeArrayRef a = UnsafeArrayOf(args, n);
  if (a.valid) {
    int64_t off = args[1].l;
    if (!UnsafeArrayRange(state, a, off, width, ok)) return false;
    UnsafeArrayWrite(a, off, slot_image, width);
    return true;
  }
  void* slot = UnsafeSlot(state, args, n, ok);
  if (!*ok) return false;
  std::memcpy(slot, &slot_image, sizeof(slot_image));
  return true;
}

Value UnsafeGetInt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 4, &v, ok)) return Value{};
  Value r{};
  r.i = static_cast<int32_t>(static_cast<uint32_t>(v));
  return r;
}

Value UnsafePutInt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  UnsafeWriteBits(state, args, n, static_cast<uint32_t>(args[2].i), 4, ok);
  return Value{};
}

Value UnsafeGetAndAddInt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 4, &v, ok)) return Value{};
  int32_t old = static_cast<int32_t>(static_cast<uint32_t>(v));
  UnsafeWriteBits(state, args, n, static_cast<uint32_t>(old + args[2].i), 4, ok);
  Value r{};
  r.i = old;
  return r;
}

Value UnsafeGetAndSetInt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 4, &v, ok)) return Value{};
  int32_t old = static_cast<int32_t>(static_cast<uint32_t>(v));
  UnsafeWriteBits(state, args, n, static_cast<uint32_t>(args[2].i), 4, ok);
  Value r{};
  r.i = old;
  return r;
}

Value UnsafeCompareAndSetInt(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 4, &v, ok)) return Value{};
  int32_t old = static_cast<int32_t>(static_cast<uint32_t>(v));
  Value r{};
  if (old == args[2].i) {
    UnsafeWriteBits(state, args, n, static_cast<uint32_t>(args[3].i), 4, ok);
    r.i = 1;
  } else {
    r.i = 0;
  }
  return r;
}

Value UnsafeCompareAndExchangeInt(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 4, &v, ok)) return Value{};
  int32_t old = static_cast<int32_t>(static_cast<uint32_t>(v));
  if (old == args[2].i) UnsafeWriteBits(state, args, n, static_cast<uint32_t>(args[3].i), 4, ok);
  Value r{};
  r.i = old;
  return r;
}

// ---- Unsafe long 槽面（java.util.concurrent.atomic.AtomicLong lib 本体的 CAS 承接；
// 单线程 VM：CAS = 比较后交换）----
Value UnsafeGetLong(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 8, &v, ok)) return Value{};
  Value r{};
  r.l = static_cast<int64_t>(v);
  return r;
}

Value UnsafePutLong(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  UnsafeWriteBits(state, args, n, static_cast<uint64_t>(args[2].l), 8, ok);
  return Value{};
}

Value UnsafeGetAndAddLong(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 8, &v, ok)) return Value{};
  int64_t old = static_cast<int64_t>(v);
  UnsafeWriteBits(state, args, n, static_cast<uint64_t>(old + args[2].l), 8, ok);
  Value r{};
  r.l = old;
  return r;
}

Value UnsafeGetAndSetLong(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 8, &v, ok)) return Value{};
  int64_t old = static_cast<int64_t>(v);
  UnsafeWriteBits(state, args, n, static_cast<uint64_t>(args[2].l), 8, ok);
  Value r{};
  r.l = old;
  return r;
}

Value UnsafeCompareAndSetLong(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 8, &v, ok)) return Value{};
  int64_t old = static_cast<int64_t>(v);
  Value r{};
  if (old == args[2].l) {
    UnsafeWriteBits(state, args, n, static_cast<uint64_t>(args[3].l), 8, ok);
    r.i = 1;
  } else {
    r.i = 0;
  }
  return r;
}

Value UnsafeCompareAndExchangeLong(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 8, &v, ok)) return Value{};
  int64_t old = static_cast<int64_t>(v);
  if (old == args[2].l) UnsafeWriteBits(state, args, n, static_cast<uint64_t>(args[3].l), 8, ok);
  Value r{};
  r.l = old;
  return r;
}

// ---- Unsafe 对象槽面（8 字节槽存强引用；GC 按 kWord 扫描，指针全宽）----
// putObjectVolatile/getObjectVolatile/compareAndSetObject/getAndSetObject/
// putOrderedObject 族 + Reference 命名镜像（JBR 25 形状漂移同型）。
// 单线程 VM：volatile/ordered = 普通访问；CAS = 比较后交换（指针同一性）。
namespace {
ObjHeader* UnsafeLoadObject(void* slot) {
  ObjHeader* v = nullptr;
  std::memcpy(&v, slot, sizeof(v));
  return v;
}

void UnsafeStoreObject(void* slot, ObjHeader* v) {
  std::memcpy(slot, &v, sizeof(v));
}

}  // namespace
Value UnsafeGetObject(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  void* slot = UnsafeSlot(state, args, n, ok);
  if (!*ok) return Value{};
  Value r{};
  r.ref = UnsafeLoadObject(slot);
  return r;
}

Value UnsafePutObject(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  void* slot = UnsafeSlot(state, args, n, ok);
  if (!*ok) return Value{};
  UnsafeStoreObject(slot, args[2].ref);
  return Value{};
}

Value UnsafeGetAndSetObject(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  void* slot = UnsafeSlot(state, args, n, ok);
  if (!*ok) return Value{};
  ObjHeader* old = UnsafeLoadObject(slot);
  UnsafeStoreObject(slot, args[2].ref);
  Value r{};
  r.ref = old;
  return r;
}

Value UnsafeCompareAndSetObject(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)self;
  void* slot = UnsafeSlot(state, args, n, ok);
  if (!*ok) return Value{};
  ObjHeader* old = UnsafeLoadObject(slot);
  Value r{};
  if (old == args[2].ref) {
    UnsafeStoreObject(slot, args[3].ref);
    r.i = 1;
  } else {
    r.i = 0;
  }
  return r;
}

Value UnsafeCompareAndExchangeObject(VMState& state, VObject* self, const Value* args, size_t n,
                                     bool* ok) {
  (void)self;
  void* slot = UnsafeSlot(state, args, n, ok);
  if (!*ok) return Value{};
  ObjHeader* old = UnsafeLoadObject(slot);
  if (old == args[2].ref) UnsafeStoreObject(slot, args[3].ref);
  Value r{};
  r.ref = old;
  return r;
}

// 内存屏障族（单线程 VM：编译器/硬件屏障无消费面，空实现语义即真）。
Value UnsafeFence(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  return Value{};
}

// 数组基址/步长（HotSpot 暴露面）：arrayBaseOffset = 元素区相对 field_data()
// 的字节偏移（本 VM 中 VArray::length 复用 ObjHeader 尾填充，
// sizeof(VArray)==sizeof(ObjHeader)，故为 0）；arrayIndexScale = JVM 逻辑
// 元素宽度（B/Z=1、C/S=2、I/F=4、J/D/引用=8）。实际存储仍是 8 字节槽，
// typed 访问器负责 packed 逻辑偏移 -> 槽地址的转换（见 UnsafeArrayRead/Write）。
Value UnsafeArrayBaseOffset(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = static_cast<int32_t>(sizeof(VArray) - sizeof(ObjHeader));
  return r;
}

Value UnsafeArrayIndexScale(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  // 同一 intrinsic 也承载 addressSize()（无参）：地址宽度 8。
  if (n < 1 || args[0].ref == nullptr) {
    r.i = 8;
    return r;
  }
  VClass* k = reinterpret_cast<VClass*>(args[0].ref);  // guest Class 镜像即 VClass*
  r.i = k->is_array ? UnsafeArrayElemSize(k->component_type) : 8;
  return r;
}

// ---- typed 槽面（Z/B/S/C/F/D；I/J/L 已有）。窄化语义与解释器
//      NarrowRead/NarrowWrite（core/interpreter.cc）逐位一致，保证与
//      iget/aput 混用互见同一编码。数组面按 JVM packed 逻辑偏移经
//      UnsafeReadBits/UnsafeWriteBits 转换（见上）。----
Value UnsafeGetBoolean(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 1, &v, ok)) return Value{};
  Value r{};
  r.i = (v & 0xffu) != 0 ? 1 : 0;  // Narrow::kBool
  return r;
}

Value UnsafePutBoolean(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  UnsafeWriteBits(state, args, n, static_cast<uint64_t>(args[2].i) & 0xffu, 1, ok);  // kBool
  return Value{};
}

Value UnsafeGetByte(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 1, &v, ok)) return Value{};
  Value r{};
  r.i = static_cast<int32_t>(static_cast<int8_t>(v));  // Narrow::kByte
  return r;
}

// ---- 地址形字节面（jdk.internal.misc.Unsafe getByte(long)/putByte(long,byte)）----
// 绝对地址单字节直读直写（java.nio Direct/MappedByteBuffer mmap 数据面的
// 底层缝；与对象槽 kWord 语义无关）。
Value UnsafeGetByteAddress(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)self;
  if (n < 1 || args[0].l == 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "getByte(address)");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = static_cast<int32_t>(
      *reinterpret_cast<const int8_t*>(static_cast<uintptr_t>(args[0].l)));
  return r;
}

Value UnsafePutByteAddress(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)self;
  if (n < 2 || args[0].l == 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "putByte(address)");
    *ok = false;
    return Value{};
  }
  *reinterpret_cast<int8_t*>(static_cast<uintptr_t>(args[0].l)) =
      static_cast<int8_t>(args[1].i);
  return Value{};
}

// ---- 原生内存面（JDK8 sun.misc.Unsafe address 形 / allocateMemory 族）----
// Direct/MappedByteBuffer 与 Netty off-heap 池依赖：绝对地址按宿主指针直读直写
// （与既有 getByte(address) 同型）；未对齐用 memcpy，避免 ARM 对齐异常。
namespace {
bool UnsafeAddressArg(VMState& state, const Value* args, size_t n, uintptr_t* out,
                      const char* what, bool* ok) {
  if (n < 1 || args[0].l == 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", what);
    *ok = false;
    return false;
  }
  *out = static_cast<uintptr_t>(static_cast<uint64_t>(args[0].l));
  return true;
}

uint64_t UnsafeLoadAddress(uintptr_t addr, size_t width) {
  uint64_t v = 0;
  std::memcpy(&v, reinterpret_cast<const void*>(addr), width);
  return v;
}

void UnsafeStoreAddress(uintptr_t addr, uint64_t v, size_t width) {
  std::memcpy(reinterpret_cast<void*>(addr), &v, width);
}

// 对象/数组的逻辑 packed 字节视图（copyMemory/setMemory(Object,...) 用）。
bool UnsafeMemoryByteRead(VMState& state, ObjHeader* base, int64_t off, uint8_t* out, bool* ok) {
  if (base == nullptr) {
    *out = *reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(off));
    return true;
  }
  VClass* k = base->klass;
  if (k != nullptr && k->is_array) {
    UnsafeArrayRef a{reinterpret_cast<VArray*>(base),
                     UnsafeArrayElemSize(k->component_type), true};
    int64_t bytes = static_cast<int64_t>(a.arr->length) * a.elem_size;
    int64_t b = off - kArrayLogicalBase;
    if (b < 0 || b >= bytes) {
      state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;", "Unsafe memory access out of bounds");
      *ok = false;
      return false;
    }
    int64_t idx = b / a.elem_size;
    int64_t within = b - idx * a.elem_size;
    *out = a.arr->data()[idx * kArraySlotBytes + within];
    return true;
  }
  *out = static_cast<VObject*>(base)->field_data()[off];
  return true;
}

bool UnsafeMemoryByteWrite(VMState& state, ObjHeader* base, int64_t off, uint8_t value,
                           bool* ok) {
  if (base == nullptr) {
    *reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(off)) = value;
    return true;
  }
  VClass* k = base->klass;
  if (k != nullptr && k->is_array) {
    UnsafeArrayRef a{reinterpret_cast<VArray*>(base),
                     UnsafeArrayElemSize(k->component_type), true};
    int64_t bytes = static_cast<int64_t>(a.arr->length) * a.elem_size;
    int64_t b = off - kArrayLogicalBase;
    if (b < 0 || b >= bytes) {
      state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;", "Unsafe memory access out of bounds");
      *ok = false;
      return false;
    }
    int64_t idx = b / a.elem_size;
    int64_t within = b - idx * a.elem_size;
    a.arr->data()[idx * kArraySlotBytes + within] = value;
    return true;
  }
  static_cast<VObject*>(base)->field_data()[off] = value;
  return true;
}

bool UnsafeObjectRef(const Value& v, ObjHeader** out, VMState& state, bool* ok) {
  *out = v.ref;
  if (*out != nullptr && state.heap != nullptr && !state.heap->IsHeapStart(*out)) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "Unsafe memory base");
    *ok = false;
    return false;
  }
  return true;
}
}  // namespace

Value UnsafeGetShortAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (!UnsafeAddressArg(state, args, n, &addr, "getShort(address)", ok)) return Value{};
  Value r{};
  r.i = static_cast<int32_t>(static_cast<int16_t>(UnsafeLoadAddress(addr, 2)));
  return r;
}

Value UnsafePutShortAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (n < 2 || !UnsafeAddressArg(state, args, n, &addr, "putShort(address)", ok)) {
    if (n < 2) {
      state.ThrowNew("Ljava/lang/IllegalArgumentException;", "putShort(address)");
      *ok = false;
    }
    return Value{};
  }
  UnsafeStoreAddress(addr, static_cast<uint16_t>(args[1].i), 2);
  return Value{};
}

Value UnsafeGetCharAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (!UnsafeAddressArg(state, args, n, &addr, "getChar(address)", ok)) return Value{};
  Value r{};
  r.i = static_cast<int32_t>(UnsafeLoadAddress(addr, 2) & 0xffffu);
  return r;
}

Value UnsafePutCharAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (n < 2 || !UnsafeAddressArg(state, args, n, &addr, "putChar(address)", ok)) {
    if (n < 2) {
      state.ThrowNew("Ljava/lang/IllegalArgumentException;", "putChar(address)");
      *ok = false;
    }
    return Value{};
  }
  UnsafeStoreAddress(addr, static_cast<uint16_t>(args[1].i), 2);
  return Value{};
}

Value UnsafeGetIntAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (!UnsafeAddressArg(state, args, n, &addr, "getInt(address)", ok)) return Value{};
  Value r{};
  r.i = static_cast<int32_t>(static_cast<uint32_t>(UnsafeLoadAddress(addr, 4)));
  return r;
}

Value UnsafePutIntAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (n < 2 || !UnsafeAddressArg(state, args, n, &addr, "putInt(address)", ok)) {
    if (n < 2) {
      state.ThrowNew("Ljava/lang/IllegalArgumentException;", "putInt(address)");
      *ok = false;
    }
    return Value{};
  }
  UnsafeStoreAddress(addr, static_cast<uint32_t>(args[1].i), 4);
  return Value{};
}

Value UnsafeGetLongAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (!UnsafeAddressArg(state, args, n, &addr, "getLong(address)", ok)) return Value{};
  Value r{};
  r.l = static_cast<int64_t>(UnsafeLoadAddress(addr, 8));
  return r;
}

Value UnsafePutLongAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (n < 2 || !UnsafeAddressArg(state, args, n, &addr, "putLong(address)", ok)) {
    if (n < 2) {
      state.ThrowNew("Ljava/lang/IllegalArgumentException;", "putLong(address)");
      *ok = false;
    }
    return Value{};
  }
  UnsafeStoreAddress(addr, static_cast<uint64_t>(args[1].l), 8);
  return Value{};
}

Value UnsafeGetFloatAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (!UnsafeAddressArg(state, args, n, &addr, "getFloat(address)", ok)) return Value{};
  uint32_t bits = static_cast<uint32_t>(UnsafeLoadAddress(addr, 4));
  Value r{};
  std::memcpy(&r.f, &bits, sizeof(bits));
  return r;
}

Value UnsafePutFloatAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (n < 2 || !UnsafeAddressArg(state, args, n, &addr, "putFloat(address)", ok)) {
    if (n < 2) {
      state.ThrowNew("Ljava/lang/IllegalArgumentException;", "putFloat(address)");
      *ok = false;
    }
    return Value{};
  }
  uint32_t bits = 0;
  std::memcpy(&bits, &args[1].f, sizeof(bits));
  UnsafeStoreAddress(addr, bits, 4);
  return Value{};
}

Value UnsafeGetDoubleAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (!UnsafeAddressArg(state, args, n, &addr, "getDouble(address)", ok)) return Value{};
  uint64_t bits = UnsafeLoadAddress(addr, 8);
  Value r{};
  std::memcpy(&r.d, &bits, sizeof(bits));
  return r;
}

Value UnsafePutDoubleAddress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (n < 2 || !UnsafeAddressArg(state, args, n, &addr, "putDouble(address)", ok)) {
    if (n < 2) {
      state.ThrowNew("Ljava/lang/IllegalArgumentException;", "putDouble(address)");
      *ok = false;
    }
    return Value{};
  }
  uint64_t bits = 0;
  std::memcpy(&bits, &args[1].d, sizeof(bits));
  UnsafeStoreAddress(addr, bits, 8);
  return Value{};
}

Value UnsafeAllocateMemory(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  if (n < 1 || args[0].l < 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "allocateMemory");
    *ok = false;
    return Value{};
  }
  size_t bytes = static_cast<size_t>(args[0].l);
  void* p = std::malloc(bytes == 0 ? 1 : bytes);
  if (p == nullptr) {
    ThrowOOM(state, "Unsafe.allocateMemory");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.l = static_cast<int64_t>(reinterpret_cast<uintptr_t>(p));
  return r;
}

Value UnsafeReallocateMemory(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  if (n < 2 || args[1].l < 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "reallocateMemory");
    *ok = false;
    return Value{};
  }
  size_t bytes = static_cast<size_t>(args[1].l);
  void* p = args[0].l == 0
                ? std::malloc(bytes == 0 ? 1 : bytes)
                : std::realloc(reinterpret_cast<void*>(static_cast<uintptr_t>(args[0].l)),
                               bytes == 0 ? 1 : bytes);
  if (p == nullptr) {
    ThrowOOM(state, "Unsafe.reallocateMemory");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.l = static_cast<int64_t>(reinterpret_cast<uintptr_t>(p));
  return r;
}

Value UnsafeFreeMemory(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)ok;
  if (n >= 1 && args[0].l != 0) {
    std::free(reinterpret_cast<void*>(static_cast<uintptr_t>(args[0].l)));
  }
  return Value{};
}

Value UnsafeSetMemoryAddress(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  uintptr_t addr = 0;
  if (n < 3 || !UnsafeAddressArg(state, args, n, &addr, "setMemory(address)", ok)) {
    if (n < 3) {
      state.ThrowNew("Ljava/lang/IllegalArgumentException;", "setMemory(address)");
      *ok = false;
    }
    return Value{};
  }
  if (args[1].l > 0) {
    std::memset(reinterpret_cast<void*>(addr), args[2].i, static_cast<size_t>(args[1].l));
  }
  return Value{};
}

Value UnsafeSetMemoryObject(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)self;
  if (n < 4) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "setMemory(Object,...)");
    *ok = false;
    return Value{};
  }
  ObjHeader* base = nullptr;
  if (!UnsafeObjectRef(args[0], &base, state, ok)) return Value{};
  int64_t off = args[1].l;
  int64_t bytes = args[2].l;
  uint8_t value = static_cast<uint8_t>(args[3].i);
  for (int64_t i = 0; i < bytes; ++i) {
    if (!UnsafeMemoryByteWrite(state, base, off + i, value, ok)) return Value{};
  }
  return Value{};
}

Value UnsafeCopyMemoryAddress(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)self;
  if (n < 3 || args[0].l == 0 || args[1].l == 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "copyMemory(address)");
    *ok = false;
    return Value{};
  }
  if (args[2].l > 0) {
    std::memmove(reinterpret_cast<void*>(static_cast<uintptr_t>(args[1].l)),
                 reinterpret_cast<const void*>(static_cast<uintptr_t>(args[0].l)),
                 static_cast<size_t>(args[2].l));
  }
  return Value{};
}

Value UnsafeCopyMemoryObject(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  if (n < 5) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "copyMemory(Object,...)");
    *ok = false;
    return Value{};
  }
  ObjHeader* src = nullptr;
  ObjHeader* dst = nullptr;
  if (!UnsafeObjectRef(args[0], &src, state, ok)) return Value{};
  if (!UnsafeObjectRef(args[2], &dst, state, ok)) return Value{};
  int64_t src_off = args[1].l;
  int64_t dst_off = args[3].l;
  int64_t bytes = args[4].l;
  if (bytes < 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "copyMemory(Object,...)");
    *ok = false;
    return Value{};
  }
  // memmove 语义：同一基址且目标后移时反向拷贝。
  if (src == dst && dst_off > src_off && dst_off < src_off + bytes) {
    for (int64_t i = bytes - 1; i >= 0; --i) {
      uint8_t v = 0;
      if (!UnsafeMemoryByteRead(state, src, src_off + i, &v, ok)) return Value{};
      if (!UnsafeMemoryByteWrite(state, dst, dst_off + i, v, ok)) return Value{};
    }
  } else {
    for (int64_t i = 0; i < bytes; ++i) {
      uint8_t v = 0;
      if (!UnsafeMemoryByteRead(state, src, src_off + i, &v, ok)) return Value{};
      if (!UnsafeMemoryByteWrite(state, dst, dst_off + i, v, ok)) return Value{};
    }
  }
  return Value{};
}

Value UnsafePutByte(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  UnsafeWriteBits(state, args, n,
                  static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(args[2].i))),
                  1, ok);  // Narrow::kByte
  return Value{};
}

Value UnsafeGetChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 2, &v, ok)) return Value{};
  Value r{};
  r.i = static_cast<int32_t>(v & 0xffffu);  // Narrow::kChar
  return r;
}

Value UnsafePutChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  UnsafeWriteBits(state, args, n, static_cast<uint64_t>(args[2].i) & 0xffffu, 2, ok);  // kChar
  return Value{};
}

Value UnsafeGetShort(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 2, &v, ok)) return Value{};
  Value r{};
  r.i = static_cast<int32_t>(static_cast<int16_t>(v));  // Narrow::kShort
  return r;
}

Value UnsafePutShort(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  UnsafeWriteBits(state, args, n,
                  static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(args[2].i))),
                  2, ok);  // Narrow::kShort
  return Value{};
}

Value UnsafeGetFloat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t v = 0;
  if (!UnsafeReadBits(state, args, n, 4, &v, ok)) return Value{};
  uint32_t bits = static_cast<uint32_t>(v);  // Narrow::kWord
  Value r{};
  std::memcpy(&r.f, &bits, sizeof(bits));
  return r;
}

Value UnsafePutFloat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint32_t bits = 0;
  std::memcpy(&bits, &args[2].f, sizeof(bits));
  UnsafeWriteBits(state, args, n, bits, 4, ok);  // Narrow::kWord
  return Value{};
}

Value UnsafeGetDouble(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t bits = 0;
  if (!UnsafeReadBits(state, args, n, 8, &bits, ok)) return Value{};
  Value r{};
  std::memcpy(&r.d, &bits, sizeof(bits));
  return r;
}

Value UnsafePutDouble(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint64_t bits = 0;
  std::memcpy(&bits, &args[2].d, sizeof(bits));
  UnsafeWriteBits(state, args, n, bits, 8, ok);  // Narrow::kWide
  return Value{};
}

// ---- 元数据/分配面（sun.misc.Unsafe legacy 门面转发目标）----
// allocateInstance：绕构造器分配（ART jni_internal.cc AllocObject 同构：
// EnsureInitialized 先行，abstract/interface 抛 InstantiationException，
// heap Alloc 全零对象——构造器字段后置不生效）。
Value UnsafeAllocateInstance(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "allocateInstance");
    *ok = false;
    return Value{};
  }
  VClass* k = reinterpret_cast<VClass*>(args[0].ref);
  if (!state.linker->InitializeClass(k, &state)) {
    *ok = false;
    return Value{};
  }
  if (k->is_primitive || k->is_array || k->IsAbstract() || k->IsInterface() ||
      k->descriptor == "Ljava/lang/Class;") {
    state.ThrowNew("Ljava/lang/InstantiationException;", "Can't instantiate " + k->descriptor);
    *ok = false;
    return Value{};
  }
  VObject* obj = state.linker->AllocObject(k);
  if (obj == nullptr) {
    ThrowOOM(state, "allocateInstance");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = obj;
  return r;
}

// staticFieldOffset/Base：静态槽 = static_storage[slot]（Value 8 字节槽），
// 偏移带 kStaticSlotTag 标记（UnsafeSlot 分流契约）——偏移自带槽绝对地址，
// base = declaring Class（staticFieldBase 配对面；寻址不依赖 base）。
Value UnsafeStaticFieldOffset(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "staticFieldOffset");
    *ok = false;
    return Value{};
  }
  VField* f = MirrorField(static_cast<VObject*>(args[0].ref));
  if (f == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "staticFieldOffset: null field mirror");
    *ok = false;
    return Value{};
  }
  if (!f->is_static) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "not a static field: " + f->name);
    *ok = false;
    return Value{};
  }
  if (f->declaring == nullptr || f->offset >= f->declaring->static_storage.size()) {
    state.ThrowNew("Ljava/lang/NoSuchFieldError;", "static slot missing: " + f->name);
    *ok = false;
    return Value{};
  }
  uint64_t slot = reinterpret_cast<uint64_t>(&f->declaring->static_storage[f->offset]);
  Value r{};
  r.l = static_cast<int64_t>(kStaticSlotTag | slot);
  return r;
}

Value UnsafeStaticFieldBase(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "staticFieldBase");
    *ok = false;
    return Value{};
  }
  VField* f = MirrorField(static_cast<VObject*>(args[0].ref));
  if (f == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "staticFieldBase: null field mirror");
    *ok = false;
    return Value{};
  }
  if (!f->is_static) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "not a static field: " + f->name);
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(f->declaring);
  return r;
}

Value UnsafeShouldBeInitialized(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "shouldBeInitialized");
    *ok = false;
    return Value{};
  }
  VClass* k = reinterpret_cast<VClass*>(args[0].ref);
  Value r{};
  r.i = (k->status != ClassStatus::kInitialized && k->status != ClassStatus::kInitializing) ? 1 : 0;
  return r;
}

Value UnsafeEnsureClassInitialized(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "ensureClassInitialized");
    *ok = false;
    return Value{};
  }
  VClass* k = reinterpret_cast<VClass*>(args[0].ref);
  if (!state.linker->InitializeClass(k, &state)) *ok = false;
  return Value{};
}

// throwException：任意 Throwable 直抛（checked 不检查，对齐 OpenJDK 语义）
Value UnsafeThrowException(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "throwException");
    *ok = false;
    return Value{};
  }
  state.pending_exception = args[0].ref;
  *ok = false;
  return Value{};
}
// ---- java.util.concurrent.atomic 原子面（P0 翻转后为 lib 类的 native 面）----
// OpenJDK 参考实现 <clinit> 走 java.lang.invoke.VarHandle（findVarHandle）——
// VM 未落 MethodHandles 机器（java.lang.invoke 后置）：VarHandle 形态类
// （AtomicBoolean/AtomicReference/AtomicReferenceArray）的 CAS/内存序方法经
// BindWellKnownNatives 覆写到本面（value@0 槽）；AtomicLong 走 Unsafe long 槽
// （lib 本体直跑）。单线程 VM 下 CAS = 比较后交换（无并发争用面）。

// 虚分发一次（泛函面 UnaryOperator/BinaryOperator.apply 再入解释器）
bool CallVirtual(VMState& state, VObject* obj, const char* name, std::vector<std::string> params,
                 const char* ret, const Value* args, size_t n, Value* out) {
  if (obj == nullptr || obj->klass == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "call virtual on null");
    return false;
  }
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;", name);
    return false;
  }
  std::vector<Value> full;
  full.reserve(n + 1);
  Value self_v{};
  self_v.ref = obj;
  full.push_back(self_v);
  for (size_t i = 0; i < n; i++) full.push_back(args[i]);
  Value r{};
  if (!InvokeMethod(state, m, full.data(), full.size(), &r)) return false;
  if (out != nullptr) *out = r;
  return true;
}

namespace {
ObjHeader* AtomicGet(VObject* self) {
  ObjHeader* v = nullptr;
  std::memcpy(&v, self->field_data(), sizeof(v));
  return v;
}

void AtomicSet(VObject* self, ObjHeader* v) {
  std::memcpy(self->field_data(), &v, sizeof(v));
}

// 窄/宽字段槽（8 字节槽；I/Z 低 4 字节，J 全 8 字节）
}  // namespace
int32_t GetIField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  return static_cast<int32_t>(slot);
}

void SetIField(VObject* o, uint32_t off, int32_t v) {
  uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(v));
  std::memcpy(o->field_data() + off, &slot, sizeof(slot));
}

namespace {
int64_t GetLFieldBuiltin(VObject* o, uint32_t off) {
  int64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  return slot;
}

void SetLFieldBuiltin(VObject* o, uint32_t off, int64_t v) {
  std::memcpy(o->field_data() + off, &v, sizeof(v));
}

Value AtomicRefInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) AtomicSet(self, args[0].ref);
  return Value{};
}

}  // namespace
Value AtomicRefGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = AtomicGet(self);
  return r;
}

Value AtomicRefSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  AtomicSet(self, n >= 1 ? args[0].ref : nullptr);
  return Value{};
}

Value AtomicRefGetAndSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.ref = AtomicGet(self);
  AtomicSet(self, n >= 1 ? args[0].ref : nullptr);
  return r;
}

Value AtomicRefCompareAndSet(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  ObjHeader* expect = n >= 1 ? args[0].ref : nullptr;
  ObjHeader* update = n >= 2 ? args[1].ref : nullptr;
  r.i = 0;
  if (AtomicGet(self) == expect) {
    AtomicSet(self, update);
    r.i = 1;
  }
  return r;
}

Value AtomicRefCompareAndExchange(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  ObjHeader* old = AtomicGet(self);
  ObjHeader* expect = n >= 1 ? args[0].ref : nullptr;
  ObjHeader* update = n >= 2 ? args[1].ref : nullptr;
  if (old == expect) AtomicSet(self, update);
  r.ref = old;
  return r;
}

// updateAndGet/getAndUpdate/accumulateAndGet/getAndAccumulate（UnaryOperator/
// BinaryOperator.apply 泛函面；CAS 循环单线程即单轮）。
namespace {
Value AtomicRefUpdateLoop(VMState& state, VObject* self, const Value* args, size_t n, bool* ok,
                          bool return_old, bool accumulate) {
  *ok = true;
  Value r{};
  VObject* fn = n >= 1 ? reinterpret_cast<VObject*>(args[0].ref) : nullptr;
  if (fn == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "AtomicReference update fn == null");
    *ok = false;
    return r;
  }
  Value arg = {};
  if (accumulate && n >= 2) arg = args[1];
  for (int i = 0; i < 64; i++) {
    ObjHeader* prev = AtomicGet(self);
    Value p{};
    p.ref = prev;
    Value next{};
    if (accumulate) {
      Value fargs[2] = {p, arg};
      if (!CallVirtual(state, fn, "apply", {"Ljava/lang/Object;", "Ljava/lang/Object;"},
                       "Ljava/lang/Object;", fargs, 2, &next)) {
        *ok = false;
        return r;
      }
    } else {
      if (!CallVirtual(state, fn, "apply", {"Ljava/lang/Object;"}, "Ljava/lang/Object;", &p, 1,
                       &next)) {
        *ok = false;
        return r;
      }
    }
    if (AtomicGet(self) == prev) {
      AtomicSet(self, next.ref);
      r = return_old ? p : next;
      return r;
    }
  }
  state.ThrowNew("Ljava/lang/RuntimeException;", "AtomicReference update loop overflow");
  *ok = false;
  return r;
}

Value AtomicRefUpdateAndGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return AtomicRefUpdateLoop(state, self, args, n, ok, /*return_old=*/false, /*accumulate=*/false);
}

Value AtomicRefGetAndUpdate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return AtomicRefUpdateLoop(state, self, args, n, ok, /*return_old=*/true, /*accumulate=*/false);
}

Value AtomicRefAccumulateAndGet(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  return AtomicRefUpdateLoop(state, self, args, n, ok, /*return_old=*/false, /*accumulate=*/true);
}

Value AtomicRefGetAndAccumulate(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  return AtomicRefUpdateLoop(state, self, args, n, ok, /*return_old=*/true, /*accumulate=*/true);
}

// AtomicBoolean / AtomicLong（同族：RxJava 等库以之为基类；OpenJDK 参考实现走
// VarHandle——内建轻量面。value@0 槽；单线程 CAS）。
Value AtomicBoolInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) SetIField(self, 0, args[0].i != 0 ? 1 : 0);
  return Value{};
}

}  // namespace
Value AtomicBoolGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = GetIField(self, 0) != 0 ? 1 : 0;
  return r;
}

Value AtomicBoolSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  SetIField(self, 0, (n >= 1 && args[0].i != 0) ? 1 : 0);
  return Value{};
}

Value AtomicBoolGetAndSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = GetIField(self, 0) != 0 ? 1 : 0;
  SetIField(self, 0, (n >= 1 && args[0].i != 0) ? 1 : 0);
  return r;
}

Value AtomicBoolCompareAndSet(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  int32_t expect = (n >= 1 && args[0].i != 0) ? 1 : 0;
  int32_t update = (n >= 2 && args[1].i != 0) ? 1 : 0;
  r.i = 0;
  if (GetIField(self, 0) == expect) {
    SetIField(self, 0, update);
    r.i = 1;
  }
  return r;
}

Value AtomicBoolCompareAndExchange(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  int32_t old = GetIField(self, 0) != 0 ? 1 : 0;
  int32_t expect = (n >= 1 && args[0].i != 0) ? 1 : 0;
  int32_t update = (n >= 2 && args[1].i != 0) ? 1 : 0;
  if (old == expect) SetIField(self, 0, update);
  r.i = old;
  return r;
}

namespace {
Value AtomicLongInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) SetLFieldBuiltin(self, 0, args[0].l);
  return Value{};
}

Value AtomicLongGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.l = GetLFieldBuiltin(self, 0);
  return r;
}

Value AtomicLongSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  SetLFieldBuiltin(self, 0, n >= 1 ? args[0].l : 0);
  return Value{};
}

Value AtomicLongGetAndSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.l = GetLFieldBuiltin(self, 0);
  SetLFieldBuiltin(self, 0, n >= 1 ? args[0].l : 0);
  return r;
}

Value AtomicLongCompareAndSet(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  int64_t expect = n >= 1 ? args[0].l : 0;
  int64_t update = n >= 2 ? args[1].l : 0;
  r.i = 0;
  if (GetLFieldBuiltin(self, 0) == expect) {
    SetLFieldBuiltin(self, 0, update);
    r.i = 1;
  }
  return r;
}

Value AtomicLongCompareAndExchange(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  int64_t old = GetLFieldBuiltin(self, 0);
  int64_t expect = n >= 1 ? args[0].l : 0;
  int64_t update = n >= 2 ? args[1].l : 0;
  if (old == expect) SetLFieldBuiltin(self, 0, update);
  r.l = old;
  return r;
}

Value AtomicLongGetAndAdd(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  int64_t old = GetLFieldBuiltin(self, 0);
  SetLFieldBuiltin(self, 0, old + (n >= 1 ? args[0].l : 0));
  r.l = old;
  return r;
}

Value AtomicLongAddAndGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r = AtomicLongGetAndAdd(state, self, args, n, ok);
  if (*ok) r.l += (n >= 1 ? args[0].l : 0);
  return r;
}

Value AtomicLongIncrement(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  int64_t v = GetLFieldBuiltin(self, 0) + 1;
  SetLFieldBuiltin(self, 0, v);
  r.l = v;
  return r;
}

Value AtomicLongDecrement(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r = AtomicLongIncrement(state, self, args, n, ok);
  if (*ok) r.l -= 2;
  if (*ok) SetLFieldBuiltin(self, 0, r.l);
  return r;
}

Value AtomicLongGetAndIncrement(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  int64_t old = GetLFieldBuiltin(self, 0);
  SetLFieldBuiltin(self, 0, old + 1);
  r.l = old;
  return r;
}

Value AtomicLongGetAndDecrement(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  int64_t old = GetLFieldBuiltin(self, 0);
  SetLFieldBuiltin(self, 0, old - 1);
  r.l = old;
  return r;
}

// AtomicReferenceArray（RxJava 基类面；array 字段 @0 = [Ljava/lang/Object;）
VArray* AtomicArrData(VObject* self) {
  VArray* a = nullptr;
  std::memcpy(&a, self->field_data(), sizeof(a));
  return a;
}

ObjHeader* AtomicArrLoad(VArray* a, int32_t i) {
  ObjHeader* v = nullptr;
  if (a != nullptr && i >= 0 && i < a->length) {
    std::memcpy(&v, a->data() + static_cast<size_t>(i) * 8, sizeof(v));
  }
  return v;
}

void AtomicArrStore(VArray* a, int32_t i, ObjHeader* v) {
  if (a != nullptr && i >= 0 && i < a->length) {
    std::memcpy(a->data() + static_cast<size_t>(i) * 8, &v, sizeof(v));
  }
}

Value AtomicArrInitLen(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  int32_t len = n >= 1 ? args[0].i : 0;
  VClass* obj = state.linker->FindClass("Ljava/lang/Object;");
  VClass* arr_cls = obj != nullptr ? state.linker->FindArrayClass(obj) : nullptr;
  VArray* a = arr_cls != nullptr ? state.linker->AllocArray(arr_cls, len) : nullptr;
  if (a == nullptr) {
    ThrowOOM(state, "AtomicReferenceArray alloc failed");
    *ok = false;
    return Value{};
  }
  std::memcpy(self->field_data(), &a, sizeof(a));
  return Value{};
}

Value AtomicArrInitArr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  VArray* src = n >= 1 ? reinterpret_cast<VArray*>(args[0].ref) : nullptr;
  if (src == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "AtomicReferenceArray: array == null");
    *ok = false;
    return Value{};
  }
  VClass* obj = state.linker->FindClass("Ljava/lang/Object;");
  VClass* arr_cls = obj != nullptr ? state.linker->FindArrayClass(obj) : nullptr;
  VArray* a = arr_cls != nullptr ? state.linker->AllocArray(arr_cls, src->length) : nullptr;
  if (a == nullptr) {
    ThrowOOM(state, "AtomicReferenceArray alloc failed");
    *ok = false;
    return Value{};
  }
  for (int32_t i = 0; i < src->length; i++) AtomicArrStore(a, i, AtomicArrLoad(src, i));
  std::memcpy(self->field_data(), &a, sizeof(a));
  return Value{};
}

}  // namespace
Value AtomicArrGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.ref = AtomicArrLoad(AtomicArrData(self), n >= 1 ? args[0].i : 0);
  return r;
}

Value AtomicArrSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  AtomicArrStore(AtomicArrData(self), n >= 1 ? args[0].i : 0, n >= 2 ? args[1].ref : nullptr);
  return Value{};
}

Value AtomicArrGetAndSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  VArray* a = AtomicArrData(self);
  int32_t i = n >= 1 ? args[0].i : 0;
  r.ref = AtomicArrLoad(a, i);
  AtomicArrStore(a, i, n >= 2 ? args[1].ref : nullptr);
  return r;
}

Value AtomicArrCompareAndSet(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = 0;
  VArray* a = AtomicArrData(self);
  int32_t i = n >= 1 ? args[0].i : 0;
  ObjHeader* expect = n >= 2 ? args[1].ref : nullptr;
  ObjHeader* update = n >= 3 ? args[2].ref : nullptr;
  if (AtomicArrLoad(a, i) == expect) {
    AtomicArrStore(a, i, update);
    r.i = 1;
  }
  return r;
}

namespace {
Value AtomicArrLength(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VArray* a = AtomicArrData(self);
  r.i = a != nullptr ? a->length : 0;
  return r;
}

}  // namespace
Value AtomicArrCompareAndExchange(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  VArray* a = AtomicArrData(self);
  int32_t i = n >= 1 ? args[0].i : 0;
  ObjHeader* expect = n >= 2 ? args[1].ref : nullptr;
  ObjHeader* update = n >= 3 ? args[2].ref : nullptr;
  ObjHeader* old = AtomicArrLoad(a, i);
  if (old == expect) AtomicArrStore(a, i, update);
  r.ref = old;
  return r;
}

// ---- java.util.concurrent.atomic.*FieldUpdater（lib native 面，AOSP 同形）----
// vendored 三类的 native 方法面由 VM 承接：newUpdater 解析目标字段槽偏移
// （超类链回溯；static/volatile/类型校验按 Android 语义），updater 实例 ->
// 偏移经侧表（klass 校验防 GC 后地址复用，同 monitor 侧表口径）。
// get/set/CAS/自增等按 8 字节槽直访（int 走 kWord 窄化，同 Unsafe 面）。
namespace {
struct FieldUpdaterRec {
  VClass* klass = nullptr;  // updater 实例类（地址复用校验）
  uint32_t offset = 0;      // 目标字段槽偏移（field_data() 基准）
};
std::unordered_map<VObject*, FieldUpdaterRec> g_field_updaters;

FieldUpdaterRec* FindFieldUpdater(VObject* self) {
  if (self == nullptr) return nullptr;
  auto it = g_field_updaters.find(self);
  if (it == g_field_updaters.end() || it->second.klass != self->klass) return nullptr;
  return &it->second;
}

FieldUpdaterRec* RequireFieldUpdater(VMState& state, VObject* self, bool* ok) {
  FieldUpdaterRec* rec = FindFieldUpdater(self);
  if (rec == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "field updater not initialized");
    *ok = false;
  }
  return rec;
}

// newUpdater 公共面：tclass + fieldName（want_desc 非空 = 字段类型校验）。
VObject* NewFieldUpdater(VMState& state, const char* updater_desc, VClass* tclass,
                         const char* want_desc, VObject* name_obj, bool* ok) {
  if (tclass == nullptr || name_obj == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "newUpdater args");
    *ok = false;
    return nullptr;
  }
  std::string field_name =
      static_cast<VString*>(static_cast<ObjHeader*>(name_obj))->ToModifiedUtf8();
  VField* f = FindInstField(tclass, field_name.c_str());
  if (f == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "No such field: " + field_name);
    *ok = false;
    return nullptr;
  }
  if (f->is_static) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "Cannot be static: " + field_name);
    *ok = false;
    return nullptr;
  }
  if ((f->access_flags & 0x40u) == 0) {  // ACC_VOLATILE
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "Must be volatile type: " + field_name);
    *ok = false;
    return nullptr;
  }
  if (want_desc != nullptr && f->type_descriptor != want_desc) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "Field type mismatch: " + field_name);
    *ok = false;
    return nullptr;
  }
  VClass* uk = state.linker->FindClass(updater_desc);
  if (uk == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "updater class missing");
    *ok = false;
    return nullptr;
  }
  VObject* u = state.linker->AllocObject(uk);
  if (u == nullptr) {
    ThrowOOM(state, "field updater");
    *ok = false;
    return nullptr;
  }
  FieldUpdaterRec rec;
  rec.klass = uk;
  rec.offset = f->offset;
  g_field_updaters[u] = rec;
  *ok = true;
  return u;
}

VObject* UpdaterTarget(VMState& state, const Value* args, size_t n, bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "updater target");
    *ok = false;
    return nullptr;
  }
  return static_cast<VObject*>(args[0].ref);
}

void* UpdaterSlot(VObject* obj, FieldUpdaterRec* rec) {
  return obj->field_data() + rec->offset;
}

ObjHeader* UpdaterLoadRef(void* slot) {
  ObjHeader* v = nullptr;
  std::memcpy(&v, slot, sizeof(v));
  return v;
}
void UpdaterStoreRef(void* slot, ObjHeader* v) { std::memcpy(slot, &v, sizeof(v)); }

int64_t UpdaterLoadLong(void* slot) {
  int64_t v = 0;
  std::memcpy(&v, slot, sizeof(v));
  return v;
}
void UpdaterStoreLong(void* slot, int64_t v) { std::memcpy(slot, &v, sizeof(v)); }
}  // namespace

// -- newUpdater（三类静态面）--
Value UpdaterRefNew(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  VClass* tclass = n >= 1 ? reinterpret_cast<VClass*>(args[0].ref) : nullptr;
  VClass* vclass = n >= 2 ? reinterpret_cast<VClass*>(args[1].ref) : nullptr;
  VObject* name = n >= 3 ? static_cast<VObject*>(args[2].ref) : nullptr;
  Value r{};
  r.ref = NewFieldUpdater(state, "Ljava/util/concurrent/atomic/AtomicReferenceFieldUpdater;",
                          tclass, vclass != nullptr ? vclass->descriptor.c_str() : nullptr, name,
                          ok);
  return r;
}
Value UpdaterIntNew(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  VClass* tclass = n >= 1 ? reinterpret_cast<VClass*>(args[0].ref) : nullptr;
  VObject* name = n >= 2 ? static_cast<VObject*>(args[1].ref) : nullptr;
  Value r{};
  r.ref = NewFieldUpdater(state, "Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;",
                          tclass, "I", name, ok);
  return r;
}
Value UpdaterLongNew(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  VClass* tclass = n >= 1 ? reinterpret_cast<VClass*>(args[0].ref) : nullptr;
  VObject* name = n >= 2 ? static_cast<VObject*>(args[1].ref) : nullptr;
  Value r{};
  r.ref = NewFieldUpdater(state, "Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;",
                          tclass, "J", name, ok);
  return r;
}

// -- AtomicReferenceFieldUpdater 实例面 --
Value UpdaterRefGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  Value r{};
  r.ref = UpdaterLoadRef(UpdaterSlot(obj, rec));
  *ok = true;
  return r;
}
Value UpdaterRefSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  UpdaterStoreRef(UpdaterSlot(obj, rec), n >= 2 ? args[1].ref : nullptr);
  *ok = true;
  return Value{};
}
Value UpdaterRefCompareAndSet(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  void* slot = UpdaterSlot(obj, rec);
  ObjHeader* old = UpdaterLoadRef(slot);
  bool matched = old == (n >= 2 ? args[1].ref : nullptr);
  if (matched) UpdaterStoreRef(slot, n >= 3 ? args[2].ref : nullptr);
  Value r{};
  r.i = matched ? 1 : 0;
  *ok = true;
  return r;
}

// -- AtomicIntegerFieldUpdater 实例面 --
Value UpdaterIntGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  Value r{};
  r.i = UnsafeLoadInt(UpdaterSlot(obj, rec));
  *ok = true;
  return r;
}
Value UpdaterIntSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  UnsafeStoreInt(UpdaterSlot(obj, rec), n >= 2 ? args[1].i : 0);
  *ok = true;
  return Value{};
}
Value UpdaterIntCompareAndSet(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  void* slot = UpdaterSlot(obj, rec);
  int32_t old = UnsafeLoadInt(slot);
  bool matched = old == (n >= 2 ? args[1].i : 0);
  if (matched) UnsafeStoreInt(slot, n >= 3 ? args[2].i : 0);
  Value r{};
  r.i = matched ? 1 : 0;
  *ok = true;
  return r;
}
Value UpdaterIntGetAndAdd(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  void* slot = UpdaterSlot(obj, rec);
  int32_t old = UnsafeLoadInt(slot);
  int32_t delta = n >= 2 ? args[1].i : 0;
  UnsafeStoreInt(slot, old + delta);
  Value r{};
  r.i = old;
  *ok = true;
  return r;
}
Value UpdaterIntAddAndGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  void* slot = UpdaterSlot(obj, rec);
  int32_t next = UnsafeLoadInt(slot) + (n >= 2 ? args[1].i : 0);
  UnsafeStoreInt(slot, next);
  Value r{};
  r.i = next;
  *ok = true;
  return r;
}

// -- AtomicLongFieldUpdater 实例面 --
namespace {
Value UpdaterIntAdjust(VMState& state, VObject* self, const Value* args, size_t n, int32_t delta,
                       bool return_new, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  void* slot = UpdaterSlot(obj, rec);
  int32_t old = UnsafeLoadInt(slot);
  UnsafeStoreInt(slot, old + delta);
  Value r{};
  r.i = return_new ? old + delta : old;
  *ok = true;
  return r;
}
Value UpdaterLongAdjust(VMState& state, VObject* self, const Value* args, size_t n, int64_t delta,
                        bool return_new, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  void* slot = UpdaterSlot(obj, rec);
  int64_t old = UpdaterLoadLong(slot);
  UpdaterStoreLong(slot, old + delta);
  Value r{};
  r.l = return_new ? old + delta : old;
  *ok = true;
  return r;
}
}  // namespace
Value UpdaterIntGetAndIncrement(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  return UpdaterIntAdjust(state, self, args, n, 1, false, ok);
}
Value UpdaterIntIncrementAndGet(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  return UpdaterIntAdjust(state, self, args, n, 1, true, ok);
}
Value UpdaterIntGetAndDecrement(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  return UpdaterIntAdjust(state, self, args, n, -1, false, ok);
}
Value UpdaterIntDecrementAndGet(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  return UpdaterIntAdjust(state, self, args, n, -1, true, ok);
}
Value UpdaterLongGetAndIncrement(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok) {
  return UpdaterLongAdjust(state, self, args, n, 1, false, ok);
}
Value UpdaterLongIncrementAndGet(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok) {
  return UpdaterLongAdjust(state, self, args, n, 1, true, ok);
}
Value UpdaterLongGetAndDecrement(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok) {
  return UpdaterLongAdjust(state, self, args, n, -1, false, ok);
}
Value UpdaterLongDecrementAndGet(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok) {
  return UpdaterLongAdjust(state, self, args, n, -1, true, ok);
}

Value UpdaterLongGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  Value r{};
  r.l = UpdaterLoadLong(UpdaterSlot(obj, rec));
  *ok = true;
  return r;
}
Value UpdaterLongSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  UpdaterStoreLong(UpdaterSlot(obj, rec), n >= 2 ? args[1].l : 0);
  *ok = true;
  return Value{};
}
Value UpdaterLongCompareAndSet(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  void* slot = UpdaterSlot(obj, rec);
  int64_t old = UpdaterLoadLong(slot);
  bool matched = old == (n >= 2 ? args[1].l : 0);
  if (matched) UpdaterStoreLong(slot, n >= 3 ? args[2].l : 0);
  Value r{};
  r.i = matched ? 1 : 0;
  *ok = true;
  return r;
}
Value UpdaterLongGetAndAdd(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  void* slot = UpdaterSlot(obj, rec);
  int64_t old = UpdaterLoadLong(slot);
  int64_t delta = n >= 2 ? args[1].l : 0;
  UpdaterStoreLong(slot, old + delta);
  Value r{};
  r.l = old;
  *ok = true;
  return r;
}
Value UpdaterLongAddAndGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  FieldUpdaterRec* rec = RequireFieldUpdater(state, self, ok);
  if (rec == nullptr) return Value{};
  VObject* obj = UpdaterTarget(state, args, n, ok);
  if (obj == nullptr) return Value{};
  void* slot = UpdaterSlot(obj, rec);
  int64_t next = UpdaterLoadLong(slot) + (n >= 2 ? args[1].l : 0);
  UpdaterStoreLong(slot, next);
  Value r{};
  r.l = next;
  *ok = true;
  return r;
}

// jdk.internal.misc.Unsafe.isBigEndian（java.nio.ByteOrder <clinit> 消费；
// 宿主字节序如实探测，同 StringUTF16IsBigEndian 口径）
Value UnsafeIsBigEndian(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  Value r{};
  const uint16_t probe = 1;
  r.i = *reinterpret_cast<const uint8_t*>(&probe) == 0 ? 1 : 0;
  *ok = true;
  return r;
}

}  // namespace dexvm::runtime
