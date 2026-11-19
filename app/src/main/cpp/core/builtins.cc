// VM 内置类工厂 registrar（瘦身后）：共享助手 + BindWellKnownNatives 调度聚合 +
// 内建类清单/注册断言 + RegisterBuiltins 注册聚合 + OOM 预留/钩子接缝。
// 各语义面实现按 A 档分组拆分到 core/builtins_*.cc（纯移动重构，行为不变）：
//   builtins_string.cc / builtins_system.cc / builtins_thread.cc / builtins_unsafe.cc
//   builtins_reflect.cc / builtins_io.cc / builtins_ref.cc / builtins_exceptions.cc
//   builtins_classloader.cc
// 跨 TU 共享面（助手/内建 intrinsic）声明见 core/builtins_internal.h。

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



namespace {
VClass* AllocClass(ClassLinker* linker) {
  void* mem = linker->heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) return nullptr;
  VClass* k = new (mem) VClass();
  k->status = ClassStatus::kInitialized;
  return k;
}

// instance_bytes：实例字段区字节数（对象头之外）
VClass* MakeClass(ClassLinker* linker, std::vector<VClass*>* all, const char* descriptor,
                  VClass* super, uint32_t instance_bytes, uint32_t access) {
  VClass* k = AllocClass(linker);
  if (k == nullptr) return nullptr;
  k->descriptor = descriptor;
  k->super_class = super;
  k->access_flags = access;
  uint32_t size = static_cast<uint32_t>(sizeof(ObjHeader)) + instance_bytes;
  // 超类字段区不得收缩（CCE/堆混型根因修复）：WeakReference 事故——
  // super=Reference(header+32) 传 instance_bytes=0 → object_size 回退 header+0，
  // dex 子类（ThreadLocalMap$Entry）LinkClass 从 0 续排自己的字段，Entry.value
  // 与 Reference.referent 重叠（e.get() 读到「值」→ getEntryAfterMiss 的
  // `(ThreadLocal) e.get()` 转型抛 SoftReference→ThreadLocal CCE），且
  // Reference.<init> intrinsic 写 queue@8/next@16/discovered@24 越界踩相邻对象头。
  // 口径：object_size = max(超类 object_size, header + instance_bytes)。
  if (super != nullptr && super->object_size > size) size = super->object_size;
  k->object_size = size;
  all->push_back(k);
  return k;
}

// 方法骨架（code=nullptr：String.<init> 走 StringFactory；Object/Throwable.<init> 空实现）
void AddMethodSkeleton(VClass* klass, const char* name, std::vector<std::string> param_types,
                       const char* return_type, uint32_t access) {
  VMethod m;
  m.declaring = klass;
  m.name = name;
  m.param_types = std::move(param_types);
  m.return_type = return_type;
  std::string shorty;
  shorty.push_back(return_type[0] == 'L' || return_type[0] == '[' ? 'L' : return_type[0]);
  for (const auto& p : m.param_types) {
    shorty.push_back(p[0] == 'L' || p[0] == '[' ? 'L' : p[0]);
  }
  m.shorty = shorty;
  m.access_flags = access;
  m.code = nullptr;
  klass->direct_methods.push_back(std::move(m));
}

// intrinsic 方法：骨架 + native_fn（解释器 InvokeMethod 一处派发，core/intrinsic.h）
void AddIntrinsicMethod(VClass* klass, const char* name, std::vector<std::string> param_types,
                        const char* return_type, uint32_t access, IntrinsicFn fn) {
  AddMethodSkeleton(klass, name, std::move(param_types), return_type, access);
  klass->direct_methods.back().native_fn = ToIntrinsicSlot(fn);
}

// ---- intrinsic 参数辅助 ----

}  // namespace
std::string Mutf8Arg(const Value& v) {
  if (v.ref == nullptr) return "";
  return static_cast<VString*>(v.ref)->ToModifiedUtf8();
}

bool IsNullArg(const Value& v) { return v.ref == nullptr; }

// byte[] / byte[][] / ByteBuffer → dex 字节组（InMemoryDexClassLoader 语义，
// dalvik_system_DexFile.cc:251-310：Direct buffer memcpy / byte[] 拷入）
bool ExtractDexBytes(const Value& v, std::vector<std::vector<uint8_t>>* out, std::string* err) {
  auto load_byte_array = [out](VArray* arr) {
    std::vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(arr->length));
    for (int32_t i = 0; i < arr->length; i++) {
      uint64_t slot;
      std::memcpy(&slot, arr->data() + i * 8, sizeof(slot));
      bytes.push_back(static_cast<uint8_t>(slot & 0xffu));
    }
    out->push_back(std::move(bytes));
  };
  if (IsNullArg(v)) {
    if (err != nullptr) *err = "null dex buffer";
    return false;
  }
  ObjHeader* obj = v.ref;
  if (obj->klass == nullptr) {
    if (err != nullptr) *err = "dex buffer has no class";
    return false;
  }
  const std::string& desc = obj->klass->descriptor;
  if (desc == "[B") {
    load_byte_array(static_cast<VArray*>(obj));
    return true;
  }
  if (desc == "[[B") {
    VArray* outer = static_cast<VArray*>(obj);
    for (int32_t i = 0; i < outer->length; i++) {
      uint64_t slot;
      std::memcpy(&slot, outer->data() + i * 8, sizeof(slot));
      VArray* inner = reinterpret_cast<VArray*>(static_cast<uintptr_t>(slot));
      if (inner == nullptr) {
        if (err != nullptr) *err = "null inner byte[]";
        return false;
      }
      load_byte_array(inner);
    }
    return true;
  }
  if (desc == "Ljava/nio/ByteBuffer;") {
    // 堆 ByteBuffer：lib（OpenJDK 布局）byte[] hb + int offset/position/limit
    // （hb/offset 在 ByteBuffer、position/limit 在 Buffer——按名沿继承链取）。
    // direct buffer 无 hb（既有 JNI GetDirectBufferAddress 路径后置）。
    VObject* buf = static_cast<VObject*>(obj);
    auto find_field = [](VClass* k, const char* name, const char* type) -> const VField* {
      for (VClass* c = k; c != nullptr; c = c->super_class) {
        for (const VField& f : c->instance_fields) {
          if (f.name == name && f.type_descriptor == type) return &f;
        }
      }
      return nullptr;
    };
    auto get_i32 = [&](const char* name, int32_t def) -> int32_t {
      if (const VField* f = find_field(buf->klass, name, "I")) {
        uint64_t s2 = 0;
        std::memcpy(&s2, buf->field_data() + f->offset, sizeof(s2));
        return static_cast<int32_t>(s2);
      }
      return def;
    };
    const VField* hb_f = find_field(buf->klass, "hb", "[B");
    if (hb_f != nullptr) {
      uint64_t slot = 0;
      std::memcpy(&slot, buf->field_data() + hb_f->offset, sizeof(slot));
      VArray* arr = reinterpret_cast<VArray*>(static_cast<uintptr_t>(slot));
      if (arr != nullptr) {
        // 内容窗口 = hb[offset+position, offset+limit)（wrap：0/0/len）
        int32_t off = get_i32("offset", 0);
        int32_t pos = get_i32("position", 0);
        int32_t lim = get_i32("limit", arr->length);
        int32_t begin = off + pos;
        int32_t end = off + lim;
        if (begin < 0 || end < begin || end > arr->length) {
          if (err != nullptr) *err = "ByteBuffer window out of range";
          return false;
        }
        std::vector<uint8_t> bytes;
        bytes.reserve(static_cast<size_t>(end - begin));
        for (int32_t i = begin; i < end; i++) {
          uint64_t eslot;
          std::memcpy(&eslot, arr->data() + i * 8, sizeof(eslot));
          bytes.push_back(static_cast<uint8_t>(eslot & 0xffu));
        }
        out->push_back(std::move(bytes));
        return true;
      }
    }
    if (err != nullptr) *err = "direct ByteBuffer not supported (heap hb only)";
    return false;
  }
  if (desc == "[Ljava/nio/ByteBuffer;") {
    VArray* outer = static_cast<VArray*>(obj);
    for (int32_t i = 0; i < outer->length; i++) {
      uint64_t slot;
      std::memcpy(&slot, outer->data() + i * 8, sizeof(slot));
      Value elem{};
      elem.ref = reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(slot));
      if (!ExtractDexBytes(elem, out, err)) return false;
    }
    return true;
  }
  if (err != nullptr) *err = "unsupported dex buffer type: " + desc;
  return false;
}

// 读单 .dex / .apk(.jar) 为 dex 字节组。完整形态（zip 解析）在
// session/class_loader.cc，需 DEXVM_SESSION_IO 构建（classloader_tests.sh /
// Android CMake）；宿主旧脚本编译面退化为单 .dex 直读。
bool ReadDexPathBytes(const std::string& path, std::vector<std::vector<uint8_t>>* out,
                      std::string* err) {
#ifdef DEXVM_SESSION_IO
  return session::ReadDexPathBytes(path, out, err);
#else
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (err != nullptr) *err = "cannot open: " + path;
    return false;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
  if (data.size() >= 4 && std::memcmp(data.data(), "dex\n", 4) == 0) {
    out->push_back(std::move(data));
    return true;
  }
  if (err != nullptr) {
    *err = "apk/jar dexPath requires DEXVM_SESSION_IO build: " + path;
  }
  return false;
#endif
}

// 'a.dex:b.dex' → 段列表（DexPathList 路径分隔 ':'）
std::vector<std::string> SplitPathList(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t colon = s.find(':', start);
    if (colon == std::string::npos) {
      if (start < s.size()) out.push_back(s.substr(start));
      break;
    }
    if (colon > start) out.push_back(s.substr(start, colon - start));
    start = colon + 1;
  }
  return out;
}
// ==== M3 考纲第一批（docs 14 章 28.4/29）：jre-excluded VM 内建语义面 =========
// 零壳适配：按 Android 语义建面（不识别壳、不特判、不伪造返回值）。

// ---- LibraryLoadHook（builtins.h；System.loadLibrary 语义落点，docs 07 章 16.2）----
LibraryLoadHook g_library_load_hook = nullptr;

// ---- MessagePumpHook（builtins.h；M4.3 Thread.sleep 泵点，docs 14.10 投影）----
MessagePumpHook g_message_pump_hook = nullptr;

// 8 字节全零 Value（注意：`Value{}` 花括号聚合初始化只清首成员 i，
// 高 4 字节不确定——static 存储等全宽清零必须用本辅助，JLS 默认值语义）。
namespace {
Value ZeroValue() {
  Value v;
  std::memset(&v, 0, sizeof(v));
  return v;
}

}  // namespace
void SetLibraryLoadHook(LibraryLoadHook hook) { g_library_load_hook = hook; }
LibraryLoadHook GetLibraryLoadHook() { return g_library_load_hook; }
void SetMessagePumpHook(MessagePumpHook hook) { g_message_pump_hook = hook; }

// ---- 分配失败终点 OOM 类语义（docs 12 章 22.10 AllocateInternalWithGc）----
// 重试链（Heap::Alloc 失败→GC→重试，kMaxGcAttempts=5）耗尽后抛真
// java.lang.OutOfMemoryError。堆有余量走 ThrowNew 正常路径；堆耗尽时
// ThrowNew 落裸兜底（klass==null）——换预分配单例（ART pre-allocated OOM：
// 堆未满时预建 + Heap::Pin 钉住根，gc.cc 标记恒保活）。
namespace {

std::unordered_map<VMState*, ObjHeader*> g_oom_reserves;

// detailMessage 沿继承链字段写（同 exception.cc SetDetailMessage 语义；
// 堆耗尽时字符串建不了——保留旧文案）。
void SetOomDetail(VMState& state, ObjHeader* ex, std::string_view msg) {
  if (ex == nullptr || ex->klass == nullptr || msg.empty() || state.linker == nullptr) return;
  VField* f = nullptr;
  for (VClass* k = ex->klass; k != nullptr && f == nullptr; k = k->super_class) {
    for (auto& fd : k->instance_fields) {
      if (fd.name == "detailMessage") {
        f = &fd;
        break;
      }
    }
  }
  if (f == nullptr) return;
  std::u16string chars;
  chars.reserve(msg.size());
  for (char c : msg) chars.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
  VString* s = state.linker->AllocString(chars);
  if (s == nullptr) return;
  ObjHeader** slot = reinterpret_cast<ObjHeader**>(reinterpret_cast<VObject*>(ex)->field_data() +
                                                  f->offset);
  *slot = reinterpret_cast<ObjHeader*>(s);
}

}  // namespace

ObjHeader* EnsureOomReserve(VMState& state) {
  auto it = g_oom_reserves.find(&state);
  if (it != g_oom_reserves.end() && it->second != nullptr) return it->second;
  if (state.linker == nullptr || state.heap == nullptr) return nullptr;
  VClass* k = state.linker->FindClass("Ljava/lang/OutOfMemoryError;");
  if (k == nullptr) return nullptr;
  ObjHeader* ex = reinterpret_cast<ObjHeader*>(state.linker->AllocObject(k));
  if (ex == nullptr) return nullptr;
  g_oom_reserves[&state] = ex;
  state.heap->Pin(ex);  // 钉住根（Sweep 恒保留 + gc.cc 传递闭包标记）
  return ex;
}

void ThrowOOM(VMState& state, std::string_view msg) {
  // 预建单例尽力先行（首次 OOM 时堆通常尚有余量；已建幂等），
  // 保证后续真·堆耗尽的 OOM 仍有真类可抛。
  EnsureOomReserve(state);
  state.ThrowNew("Ljava/lang/OutOfMemoryError;", msg);
  if (state.pending_exception != nullptr && state.pending_exception->klass == nullptr) {
    // 堆耗尽：ThrowNew 落裸兜底（klass==null 不可 catch）——换预分配真 OOM。
    ObjHeader* reserve = EnsureOomReserve(state);
    if (reserve != nullptr) {
      SetOomDetail(state, reserve, msg);
      state.Throw(reserve);  // 重捕获 guest 栈快照 + 挂真类
    }
  }
}

void UpgradeBareOomIfExhausted(VMState& state) {
  ObjHeader* ex = state.pending_exception;
  if (ex == nullptr || ex->klass != nullptr || state.heap == nullptr) return;
  // 归因判据（runtime_contract 诊断口径）：used≈capacity 即 OOM 归因；
  // 堆有余量的裸对象 = 异常类缺失，不升级。
  if (state.heap->bytes_allocated() + 64 * 1024 < state.heap->capacity()) return;
  ObjHeader* reserve = EnsureOomReserve(state);
  if (reserve == nullptr) return;
  state.pending_exception = reserve;  // 保持已捕获的 last_throw_backtrace（抛点栈）
}
namespace {
// 恒真探针（AtomicLong.VMSupportsCS8：8 字节 CAS 语义单线程 VM 恒支持）
Value IntrinsicTrue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 1;
  return r;
}
}  // namespace
void BindWellKnownNatives(VClass* klass) {
  if (klass == nullptr) return;
  struct Known {
    const char* cls;
    const char* name;
    std::vector<std::string> params;
    const char* ret;
    IntrinsicFn fn;
  };
  static const Known kKnown[] = {
      {"Ljava/lang/Float;", "floatToRawIntBits", {"F"}, "I", FloatFloatToRawIntBits},
      {"Ljava/lang/Float;", "intBitsToFloat", {"I"}, "F", FloatIntBitsToFloat},
      {"Ljava/lang/Double;", "doubleToRawLongBits", {"D"}, "J", DoubleDoubleToRawLongBits},
      {"Ljava/lang/Double;", "longBitsToDouble", {"J"}, "D", DoubleLongBitsToDouble},
      // java.lang.StrictMath（lib native 面：fdlibm 超越函数，libm 直通）
      {"Ljava/lang/StrictMath;", "sin", {"D"}, "D", MathSin},
      {"Ljava/lang/StrictMath;", "cos", {"D"}, "D", MathCos},
      {"Ljava/lang/StrictMath;", "tan", {"D"}, "D", MathTan},
      {"Ljava/lang/StrictMath;", "asin", {"D"}, "D", MathAsin},
      {"Ljava/lang/StrictMath;", "acos", {"D"}, "D", MathAcos},
      {"Ljava/lang/StrictMath;", "atan", {"D"}, "D", MathAtan},
      {"Ljava/lang/StrictMath;", "atan2", {"D", "D"}, "D", MathAtan2},
      {"Ljava/lang/StrictMath;", "exp", {"D"}, "D", MathExp},
      {"Ljava/lang/StrictMath;", "log", {"D"}, "D", MathLog},
      {"Ljava/lang/StrictMath;", "log10", {"D"}, "D", MathLog10},
      {"Ljava/lang/StrictMath;", "log1p", {"D"}, "D", MathLog1p},
      {"Ljava/lang/StrictMath;", "expm1", {"D"}, "D", MathExpm1},
      {"Ljava/lang/StrictMath;", "sqrt", {"D"}, "D", MathSqrt},
      {"Ljava/lang/StrictMath;", "sinh", {"D"}, "D", MathSinh},
      {"Ljava/lang/StrictMath;", "cosh", {"D"}, "D", MathCosh},
      {"Ljava/lang/StrictMath;", "tanh", {"D"}, "D", MathTanh},
      {"Ljava/lang/StrictMath;", "IEEEremainder", {"D", "D"}, "D", MathIEEERemainder},
      // java.io 文件 IO native 面（Q2：/dev/urandom 链 + java.io 文件面；POSIX 直通）
      {"Ljava/io/FileInputStream;", "initIDs", {}, "V", FISInitIDs},
      {"Ljava/io/FileInputStream;", "open0", {"Ljava/lang/String;"}, "V", FISOpen0},
      {"Ljava/io/FileInputStream;", "read0", {}, "I", FISRead0},
      {"Ljava/io/FileInputStream;", "readBytes", {"[B", "I", "I"}, "I", FISReadBytes},
      {"Ljava/io/FileInputStream;", "skip0", {"J"}, "J", FISSkip0},
      {"Ljava/io/FileInputStream;", "available0", {}, "I", FISAvailable0},
      {"Ljava/io/FileDescriptor;", "initIDs", {}, "V", FDInitIDs},
      {"Ljava/io/FileDescriptor;", "close0", {}, "V", FDClose0},
      {"Ljava/io/FileDescriptor;", "sync", {}, "V", FDSync},
      {"Ljava/io/FileDescriptor;", "getHandle", {"I"}, "J", FDGetHandle},
      {"Ljava/io/FileDescriptor;", "getAppend", {"I"}, "Z", FDGetAppend},
      {"Ljava/io/FileCleanable;", "cleanupClose0", {"I", "J"}, "V", FCCleanupClose0},
      {"Ljava/net/SocketCleanable;", "cleanupClose0", {"I"}, "V", FCCleanupClose0},
      // java.io.RandomAccessFile native 面（sun-compat path 键直通；java.io 文件面）
      {"Ljava/io/RandomAccessFile;", "nativePrepare", {"Ljava/lang/String;", "Z"}, "Z",
       RafNativePrepare},
      // getFD/seek 的共享 fd 面：AOSP 语义 RAF 持有真实 fd，seek=lseek，
      // FileOutputStream(getFD()) 的 write 才与 RAF 的 seek 偏移一致
      // （FileDownloadRandomAccessFile 分段下载依赖）。
      {"Ljava/io/RandomAccessFile;", "nativeGetFD",
       {"Ljava/lang/String;", "Z"}, "Ljava/io/FileDescriptor;", RafNativeGetFD},
      {"Ljava/io/RandomAccessFile;", "nativeSeekFd",
       {"Ljava/io/FileDescriptor;", "J"}, "V", RafNativeSeekFd},
      {"Ljava/io/RandomAccessFile;", "nativeSize", {"Ljava/lang/String;"}, "J", RafNativeSize},
      {"Ljava/io/RandomAccessFile;", "nativeReadAt",
       {"Ljava/lang/String;", "J", "[B", "I", "I"}, "I", RafNativeReadAt},
      {"Ljava/io/RandomAccessFile;", "nativeWriteAt",
       {"Ljava/lang/String;", "J", "[B", "I", "I"}, "I", RafNativeWriteAt},
      {"Ljava/io/RandomAccessFile;", "nativeSetLength", {"Ljava/lang/String;", "J"}, "V",
       RafNativeSetLength},
      // java.nio.channels.FileChannel nativeMap（mmap 面：POSIX mmap 直通，
      // 返回 lib java.nio.DirectByteBuffer 数据面）
      {"Ljava/nio/channels/FileChannel;", "nativeMap",
       {"Ljava/lang/String;", "Z", "J", "J"}, "Ljava/nio/MappedByteBuffer;", FCNativeMap},
      // java.io.File native 面（AOSP 形态；nativeToURI/nativeDeleteOnExit 后置）
      {"Ljava/io/File;", "resolvePath", {"Ljava/lang/String;", "Ljava/lang/String;"},
       "Ljava/lang/String;", FileResolvePath},
      {"Ljava/io/File;", "nativeExists", {"Ljava/lang/String;"}, "Z", FileNativeExists},
      {"Ljava/io/File;", "nativeIsFile", {"Ljava/lang/String;"}, "Z", FileNativeIsFile},
      {"Ljava/io/File;", "nativeIsDirectory", {"Ljava/lang/String;"}, "Z", FileNativeIsDirectory},
      {"Ljava/io/File;", "nativeLength", {"Ljava/lang/String;"}, "J", FileNativeLength},
      {"Ljava/io/File;", "nativeCanRead", {"Ljava/lang/String;"}, "Z", FileNativeCanRead},
      {"Ljava/io/File;", "nativeCanWrite", {"Ljava/lang/String;"}, "Z", FileNativeCanWrite},
      {"Ljava/io/File;", "nativeCanExecute", {"Ljava/lang/String;"}, "Z", FileNativeCanExecute},
      {"Ljava/io/File;", "nativeSetReadable", {"Ljava/lang/String;", "Z", "Z"}, "Z",
       FileNativeSetReadable},
      {"Ljava/io/File;", "nativeSetWritable", {"Ljava/lang/String;", "Z", "Z"}, "Z",
       FileNativeSetWritable},
      {"Ljava/io/File;", "nativeSetExecutable", {"Ljava/lang/String;", "Z", "Z"}, "Z",
       FileNativeSetExecutable},
      {"Ljava/io/File;", "nativeLastModified", {"Ljava/lang/String;"}, "J",
       FileNativeLastModified},
      {"Ljava/io/File;", "nativeSetLastModified", {"Ljava/lang/String;", "J"}, "Z",
       FileNativeSetLastModified},
      {"Ljava/io/File;", "nativeSpace", {"Ljava/lang/String;", "I"}, "J", FileNativeSpace},
      {"Ljava/io/File;", "nativeMkdir", {"Ljava/lang/String;", "Z"}, "Z", FileNativeMkdir},
      {"Ljava/io/File;", "nativeCreate", {"Ljava/lang/String;"}, "Z", FileNativeCreate},
      {"Ljava/io/File;", "nativeDelete", {"Ljava/lang/String;"}, "Z", FileNativeDelete},
      {"Ljava/io/File;", "nativeRename", {"Ljava/lang/String;", "Ljava/lang/String;"}, "Z",
       FileNativeRename},
      {"Ljava/io/File;", "nativeList", {"Ljava/lang/String;"}, "[Ljava/lang/String;",
       FileNativeList},
      // java.io.FileOutputStream native 面（java.io 文件面补全；POSIX 直通。
      // SocketOutputStream extends FOS：<clinit> initIDs 依赖）
      {"Ljava/io/FileOutputStream;", "initIDs", {}, "V", NoopClinit},
      {"Ljava/io/FileOutputStream;", "open0", {"Ljava/lang/String;", "Z"}, "V", FOSOpen0},
      {"Ljava/io/FileOutputStream;", "write", {"I", "Z"}, "V", FOSWriteByte},
      {"Ljava/io/FileOutputStream;", "writeBytes", {"[B", "I", "I", "Z"}, "V", FOSWriteBytes},
      // java.net socket native 面（Q2：java.net socket 缝；POSIX 直通。签名按
      // vendored runtime-libs java.net 源实际 native 引用清单）
      {"Ljava/net/PlainSocketImpl;", "socketCreate", {"Z"}, "V", NetSocketCreate},
      {"Ljava/net/PlainSocketImpl;", "socketConnect", {"Ljava/net/InetAddress;", "I", "I"},
       "V", NetSocketConnect},
      {"Ljava/net/PlainSocketImpl;", "socketBind", {"Ljava/net/InetAddress;", "I"}, "V",
       NetSocketBind},
      {"Ljava/net/PlainSocketImpl;", "socketListen", {"I"}, "V", NetSocketListen},
      {"Ljava/net/PlainSocketImpl;", "socketAccept", {"Ljava/net/SocketImpl;"}, "V",
       NetSocketAccept},
      {"Ljava/net/PlainSocketImpl;", "socketAvailable", {}, "I", NetSocketAvailable},
      {"Ljava/net/PlainSocketImpl;", "socketClose0", {"Z"}, "V", NetSocketClose0},
      {"Ljava/net/PlainSocketImpl;", "socketShutdown", {"I"}, "V", NetSocketShutdown},
      {"Ljava/net/PlainSocketImpl;", "initProto", {}, "V", NoopClinit},
      {"Ljava/net/PlainSocketImpl;", "socketSetOption0", {"I", "Z", "Ljava/lang/Object;"}, "V",
       NetSocketSetOption0},
      {"Ljava/net/PlainSocketImpl;", "socketGetOption", {"I", "Ljava/lang/Object;"}, "I",
       NetSocketGetOption},
      {"Ljava/net/PlainSocketImpl;", "socketSendUrgentData", {"I"}, "V",
       NetSocketSendUrgentData},
      {"Ljava/net/SocketInputStream;", "socketRead0",
       {"Ljava/io/FileDescriptor;", "[B", "I", "I", "I"}, "I", NetSocketRead0},
      {"Ljava/net/SocketInputStream;", "init", {}, "V", NoopClinit},
      {"Ljava/net/SocketOutputStream;", "socketWrite0",
       {"Ljava/io/FileDescriptor;", "[B", "I", "I"}, "V", NetSocketWrite0},
      {"Ljava/net/SocketOutputStream;", "init", {}, "V", NoopClinit},
      {"Ljava/net/AbstractPlainSocketImpl;", "isReusePortAvailable0", {}, "Z",
       NetIsReusePortAvailable0},
      // InetAddress 解析面（Inet4/6AddressImpl 共名直通；isReachable0 签名不同）
      {"Ljava/net/Inet4AddressImpl;", "getLocalHostName", {}, "Ljava/lang/String;",
       NetGetLocalHostName},
      {"Ljava/net/Inet4AddressImpl;", "lookupAllHostAddr", {"Ljava/lang/String;"},
       "[Ljava/net/InetAddress;", NetLookupAllHostAddr},
      {"Ljava/net/Inet4AddressImpl;", "getHostByAddr", {"[B"}, "Ljava/lang/String;",
       NetGetHostByAddr},
      {"Ljava/net/Inet4AddressImpl;", "isReachable0", {"[B", "I", "[B", "I"}, "Z",
       NetIsReachable0V4},
      {"Ljava/net/Inet6AddressImpl;", "getLocalHostName", {}, "Ljava/lang/String;",
       NetGetLocalHostName},
      {"Ljava/net/Inet6AddressImpl;", "lookupAllHostAddr", {"Ljava/lang/String;"},
       "[Ljava/net/InetAddress;", NetLookupAllHostAddr},
      {"Ljava/net/Inet6AddressImpl;", "getHostByAddr", {"[B"}, "Ljava/lang/String;",
       NetGetHostByAddr},
      {"Ljava/net/Inet6AddressImpl;", "isReachable0", {"[B", "I", "I", "[B", "I", "I"}, "Z",
       NetIsReachable0V6},
      {"Ljava/net/InetAddress;", "init", {}, "V", NoopClinit},
      {"Ljava/net/InetAddressImplFactory;", "isIPv6Supported", {}, "Z", NetIsIPv6Supported},
      {"Ljava/net/Inet4Address;", "init", {}, "V", NoopClinit},
      {"Ljava/net/Inet6Address;", "init", {}, "V", NoopClinit},
      // java.lang.StringUTF16 字节序探测（<clinit> HI/LO_BYTE_SHIFT 消费）
      {"Ljava/lang/StringUTF16;", "isBigEndian", {}, "Z", StringUTF16IsBigEndian},
      // java.util.zip.Inflater native 面（zlib 直通；OkHttp 透明 gzip /
      // GZIPInputStream 响应解码必需）
      {"Ljava/util/zip/Inflater;", "init", {"Z"}, "J", InflaterInit},
      {"Ljava/util/zip/Inflater;", "nativeInflate",
       {"J", "[B", "I", "I", "[B", "I", "I"}, "I", InflaterInflate},
      {"Ljava/util/zip/Inflater;", "nativeSetDictionary", {"J", "[B", "I", "I"}, "V",
       InflaterSetDictionary},
      {"Ljava/util/zip/Inflater;", "nativeRemaining", {"J"}, "I", InflaterRemaining},
      {"Ljava/util/zip/Inflater;", "nativeNeedsDictionary", {"J"}, "Z",
       InflaterNeedsDictionary},
      {"Ljava/util/zip/Inflater;", "nativeFinished", {"J"}, "Z", InflaterFinished},
      {"Ljava/util/zip/Inflater;", "nativeAdler", {"J"}, "I", InflaterAdler},
      {"Ljava/util/zip/Inflater;", "nativeReset", {"J"}, "V", InflaterReset},
      {"Ljava/util/zip/Inflater;", "nativeEnd", {"J"}, "V", InflaterEnd},
      // java.security.AccessController 特权栈 native（无 SecurityManager：action.run()）
      {"Ljava/security/AccessController;", "doPrivileged", {"Ljava/security/PrivilegedAction;"},
       "Ljava/lang/Object;", ACDoPrivileged},
      {"Ljava/security/AccessController;", "doPrivileged",
       {"Ljava/security/PrivilegedAction;", "Ljava/security/AccessControlContext;"},
       "Ljava/lang/Object;", ACDoPrivileged},
      {"Ljava/security/AccessController;", "doPrivileged",
       {"Ljava/security/PrivilegedAction;", "Ljava/security/AccessControlContext;",
        "[Ljava/security/Permission;"},
       "Ljava/lang/Object;", ACDoPrivileged},
      {"Ljava/security/AccessController;", "doPrivileged",
       {"Ljava/security/PrivilegedExceptionAction;"}, "Ljava/lang/Object;", ACDoPrivileged},
      {"Ljava/security/AccessController;", "doPrivileged",
       {"Ljava/security/PrivilegedExceptionAction;", "Ljava/security/AccessControlContext;"},
       "Ljava/lang/Object;", ACDoPrivileged},
      {"Ljava/security/AccessController;", "doPrivileged",
       {"Ljava/security/PrivilegedExceptionAction;", "Ljava/security/AccessControlContext;",
        "[Ljava/security/Permission;"},
       "Ljava/lang/Object;", ACDoPrivileged},
      {"Ljava/security/AccessController;", "getStackAccessControlContext", {},
       "Ljava/security/AccessControlContext;", ACNoContext},
      {"Ljava/security/AccessController;", "getInheritedAccessControlContext", {},
       "Ljava/security/AccessControlContext;", ACNoContext},
      // java.util.concurrent.atomic.AtomicLong（lib 本体 <clinit> 探测面）
      {"Ljava/util/concurrent/atomic/AtomicLong;", "VMSupportsCS8", {}, "Z", IntrinsicTrue},
      // java.nio.ByteBuffer.allocateDirect（netty 直接缓冲区面；mmap + DirectByteBuffer）
      {"Ljava/nio/ByteBuffer;", "allocateDirect", {"I"}, "Ljava/nio/ByteBuffer;",
       ByteBufferAllocateDirect},
      // java.net.NetworkInterface native 面（ifaddrs 枚举；netty NetUtil 探测）
      {"Ljava/net/NetworkInterface;", "init", {}, "V", NetIfInit},
      {"Ljava/net/NetworkInterface;", "getAll", {}, "[Ljava/net/NetworkInterface;",
       NetIfGetAll},
      {"Ljava/net/NetworkInterface;", "getByName0", {"Ljava/lang/String;"},
       "Ljava/net/NetworkInterface;", NetIfGetByName},
      {"Ljava/net/NetworkInterface;", "getByIndex0", {"I"}, "Ljava/net/NetworkInterface;",
       NetIfGetByIndex},
      {"Ljava/net/NetworkInterface;", "getByInetAddress0", {"Ljava/net/InetAddress;"},
       "Ljava/net/NetworkInterface;", NetIfGetByInetAddress},
      {"Ljava/net/NetworkInterface;", "isUp0", {"Ljava/lang/String;", "I"}, "Z", NetIfIsUp},
      {"Ljava/net/NetworkInterface;", "isLoopback0", {"Ljava/lang/String;", "I"}, "Z",
       NetIfIsLoopback},
      {"Ljava/net/NetworkInterface;", "supportsMulticast0", {"Ljava/lang/String;", "I"}, "Z",
       NetIfSupportsMulticast},
      {"Ljava/net/NetworkInterface;", "isP2P0", {"Ljava/lang/String;", "I"}, "Z", NetIfIsP2P},
      {"Ljava/net/NetworkInterface;", "getMacAddr0",
       {"[B", "Ljava/lang/String;", "I"}, "[B", NetIfGetMacAddr},
      {"Ljava/net/NetworkInterface;", "getMTU0", {"Ljava/lang/String;", "I"}, "I",
       NetIfGetMTU},
      // sun.nio.ch.ChannelIO（NIO 通道/选择器 native 面；netty NIO 传输）
      {"Lsun/nio/ch/ChannelIO;", "openStreamSocket", {}, "I", ChannelOpenStreamSocket},
      {"Lsun/nio/ch/ChannelIO;", "bind", {"I", "Ljava/net/InetSocketAddress;"}, "V",
       ChannelBind},
      {"Lsun/nio/ch/ChannelIO;", "listen", {"I", "I"}, "V", ChannelListen},
      {"Lsun/nio/ch/ChannelIO;", "accept", {"I"}, "I", ChannelAccept},
      {"Lsun/nio/ch/ChannelIO;", "connect", {"I", "Ljava/net/InetSocketAddress;"}, "I",
       ChannelConnect},
      {"Lsun/nio/ch/ChannelIO;", "finishConnect", {"I"}, "I", ChannelFinishConnect},
      {"Lsun/nio/ch/ChannelIO;", "readDirect",
       {"I", "Ljava/nio/ByteBuffer;", "I", "I"}, "I", ChannelReadDirect},
      {"Lsun/nio/ch/ChannelIO;", "readHeap", {"I", "[B", "I", "I"}, "I", ChannelReadHeap},
      {"Lsun/nio/ch/ChannelIO;", "writeDirect",
       {"I", "Ljava/nio/ByteBuffer;", "I", "I"}, "I", ChannelWriteDirect},
      {"Lsun/nio/ch/ChannelIO;", "writeHeap", {"I", "[B", "I", "I"}, "I", ChannelWriteHeap},
      {"Lsun/nio/ch/ChannelIO;", "closeFd", {"I"}, "V", ChannelCloseFd},
      {"Lsun/nio/ch/ChannelIO;", "setBlocking", {"I", "Z"}, "V", ChannelSetBlocking},
      {"Lsun/nio/ch/ChannelIO;", "localAddress", {"I"}, "Ljava/net/InetSocketAddress;",
       ChannelLocalAddress},
      {"Lsun/nio/ch/ChannelIO;", "remoteAddress", {"I"}, "Ljava/net/InetSocketAddress;",
       ChannelRemoteAddress},
      {"Lsun/nio/ch/ChannelIO;", "shutdownInput", {"I"}, "V", ChannelShutdownInput},
      {"Lsun/nio/ch/ChannelIO;", "shutdownOutput", {"I"}, "V", ChannelShutdownOutput},
      {"Lsun/nio/ch/ChannelIO;", "setSockOpt", {"I", "I", "I"}, "V", ChannelSetSockOpt},
      {"Lsun/nio/ch/ChannelIO;", "newWakeupPipe", {}, "[I", ChannelNewWakeupPipe},
      {"Lsun/nio/ch/ChannelIO;", "wakeupSignal", {"I"}, "V", ChannelWakeupSignal},
      {"Lsun/nio/ch/ChannelIO;", "select", {"[I", "[I", "I", "J"}, "I", ChannelSelect},
      // java.util.concurrent.atomic.*FieldUpdater（lib native 面，AOSP 同形；
      // newUpdater 槽偏移 + get/set/CAS/自增直访，实现见 builtins_unsafe.cc）
      {"Ljava/util/concurrent/atomic/AtomicReferenceFieldUpdater;", "newUpdater",
       {"Ljava/lang/Class;", "Ljava/lang/Class;", "Ljava/lang/String;"},
       "Ljava/util/concurrent/atomic/AtomicReferenceFieldUpdater;", UpdaterRefNew},
      {"Ljava/util/concurrent/atomic/AtomicReferenceFieldUpdater;", "compareAndSet",
       {"Ljava/lang/Object;", "Ljava/lang/Object;", "Ljava/lang/Object;"}, "Z",
       UpdaterRefCompareAndSet},
      {"Ljava/util/concurrent/atomic/AtomicReferenceFieldUpdater;", "weakCompareAndSet",
       {"Ljava/lang/Object;", "Ljava/lang/Object;", "Ljava/lang/Object;"}, "Z",
       UpdaterRefCompareAndSet},
      {"Ljava/util/concurrent/atomic/AtomicReferenceFieldUpdater;", "set",
       {"Ljava/lang/Object;", "Ljava/lang/Object;"}, "V", UpdaterRefSet},
      {"Ljava/util/concurrent/atomic/AtomicReferenceFieldUpdater;", "lazySet",
       {"Ljava/lang/Object;", "Ljava/lang/Object;"}, "V", UpdaterRefSet},
      {"Ljava/util/concurrent/atomic/AtomicReferenceFieldUpdater;", "get",
       {"Ljava/lang/Object;"}, "Ljava/lang/Object;", UpdaterRefGet},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "newUpdater",
       {"Ljava/lang/Class;", "Ljava/lang/String;"},
       "Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", UpdaterIntNew},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "compareAndSet",
       {"Ljava/lang/Object;", "I", "I"}, "Z", UpdaterIntCompareAndSet},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "weakCompareAndSet",
       {"Ljava/lang/Object;", "I", "I"}, "Z", UpdaterIntCompareAndSet},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "set",
       {"Ljava/lang/Object;", "I"}, "V", UpdaterIntSet},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "lazySet",
       {"Ljava/lang/Object;", "I"}, "V", UpdaterIntSet},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "get", {"Ljava/lang/Object;"},
       "I", UpdaterIntGet},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "getAndIncrement",
       {"Ljava/lang/Object;"}, "I", UpdaterIntGetAndIncrement},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "getAndDecrement",
       {"Ljava/lang/Object;"}, "I", UpdaterIntGetAndDecrement},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "getAndAdd",
       {"Ljava/lang/Object;", "I"}, "I", UpdaterIntGetAndAdd},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "incrementAndGet",
       {"Ljava/lang/Object;"}, "I", UpdaterIntIncrementAndGet},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "decrementAndGet",
       {"Ljava/lang/Object;"}, "I", UpdaterIntDecrementAndGet},
      {"Ljava/util/concurrent/atomic/AtomicIntegerFieldUpdater;", "addAndGet",
       {"Ljava/lang/Object;", "I"}, "I", UpdaterIntAddAndGet},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "newUpdater",
       {"Ljava/lang/Class;", "Ljava/lang/String;"},
       "Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", UpdaterLongNew},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "compareAndSet",
       {"Ljava/lang/Object;", "J", "J"}, "Z", UpdaterLongCompareAndSet},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "weakCompareAndSet",
       {"Ljava/lang/Object;", "J", "J"}, "Z", UpdaterLongCompareAndSet},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "set",
       {"Ljava/lang/Object;", "J"}, "V", UpdaterLongSet},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "lazySet",
       {"Ljava/lang/Object;", "J"}, "V", UpdaterLongSet},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "get", {"Ljava/lang/Object;"}, "J",
       UpdaterLongGet},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "getAndIncrement",
       {"Ljava/lang/Object;"}, "J", UpdaterLongGetAndIncrement},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "getAndDecrement",
       {"Ljava/lang/Object;"}, "J", UpdaterLongGetAndDecrement},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "getAndAdd",
       {"Ljava/lang/Object;", "J"}, "J", UpdaterLongGetAndAdd},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "incrementAndGet",
       {"Ljava/lang/Object;"}, "J", UpdaterLongIncrementAndGet},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "decrementAndGet",
       {"Ljava/lang/Object;"}, "J", UpdaterLongDecrementAndGet},
      {"Ljava/util/concurrent/atomic/AtomicLongFieldUpdater;", "addAndGet",
       {"Ljava/lang/Object;", "J"}, "J", UpdaterLongAddAndGet},
      // java.lang.reflect.Proxy 分发接缝（§31.7：lib 本体 + VM 合成/分发缝）
      {"Ljava/lang/reflect/Proxy;", "defineProxyClass0",
       {"Ljava/lang/ClassLoader;", "[Ljava/lang/Class;"}, "Ljava/lang/Class;", ProxyDefine0},
      {"Ljava/lang/reflect/Proxy;", "isProxyClass0", {"Ljava/lang/Class;"}, "Z", ProxyIsProxy0},
      // java.lang.reflect.Array（lib native 面：数组建/长/读写 VM 原语；
      // java.lang.invoke 裁剪家族参数解析 + juc ArrayList.toArray 消费面）
      {"Ljava/lang/reflect/Array;", "newArray", {"Ljava/lang/Class;", "I"},
       "Ljava/lang/Object;", ArrayNewArray},
      {"Ljava/lang/reflect/Array;", "multiNewArray", {"Ljava/lang/Class;", "[I"},
       "Ljava/lang/Object;", ArrayMultiNewArray},
      {"Ljava/lang/reflect/Array;", "getLength", {"Ljava/lang/Object;"}, "I", ArrayGetLength},
      {"Ljava/lang/reflect/Array;", "get", {"Ljava/lang/Object;", "I"}, "Ljava/lang/Object;",
       ArrayGetBoxed},
      {"Ljava/lang/reflect/Array;", "set", {"Ljava/lang/Object;", "I", "Ljava/lang/Object;"},
       "V", ArraySetBoxed},
      {"Ljava/lang/reflect/Array;", "getBoolean", {"Ljava/lang/Object;", "I"}, "Z", ArrayGetRaw},
      {"Ljava/lang/reflect/Array;", "getByte", {"Ljava/lang/Object;", "I"}, "B", ArrayGetRaw},
      {"Ljava/lang/reflect/Array;", "getChar", {"Ljava/lang/Object;", "I"}, "C", ArrayGetRaw},
      {"Ljava/lang/reflect/Array;", "getShort", {"Ljava/lang/Object;", "I"}, "S", ArrayGetRaw},
      {"Ljava/lang/reflect/Array;", "getInt", {"Ljava/lang/Object;", "I"}, "I", ArrayGetRaw},
      {"Ljava/lang/reflect/Array;", "getLong", {"Ljava/lang/Object;", "I"}, "J", ArrayGetRaw},
      {"Ljava/lang/reflect/Array;", "getFloat", {"Ljava/lang/Object;", "I"}, "F", ArrayGetRaw},
      {"Ljava/lang/reflect/Array;", "getDouble", {"Ljava/lang/Object;", "I"}, "D", ArrayGetRaw},
      {"Ljava/lang/reflect/Array;", "setBoolean", {"Ljava/lang/Object;", "I", "Z"}, "V",
       ArraySetRaw},
      {"Ljava/lang/reflect/Array;", "setByte", {"Ljava/lang/Object;", "I", "B"}, "V",
       ArraySetRaw},
      {"Ljava/lang/reflect/Array;", "setChar", {"Ljava/lang/Object;", "I", "C"}, "V",
       ArraySetRaw},
      {"Ljava/lang/reflect/Array;", "setShort", {"Ljava/lang/Object;", "I", "S"}, "V",
       ArraySetRaw},
      {"Ljava/lang/reflect/Array;", "setInt", {"Ljava/lang/Object;", "I", "I"}, "V",
       ArraySetRaw},
      {"Ljava/lang/reflect/Array;", "setLong", {"Ljava/lang/Object;", "I", "J"}, "V",
       ArraySetRaw},
      {"Ljava/lang/reflect/Array;", "setFloat", {"Ljava/lang/Object;", "I", "F"}, "V",
       ArraySetRaw},
      {"Ljava/lang/reflect/Array;", "setDouble", {"Ljava/lang/Object;", "I", "D"}, "V",
       ArraySetRaw},
      // javax.net.ssl.BridgedTls：guest TLS 薄壳 -> 宿主真 TLS 栈（core/tls_bridge.cc）
      {"Ljavax/net/ssl/BridgedTls;", "open", {"Ljava/lang/String;", "I", "I"},
       "Ljava/lang/String;", TlsNativeOpen},
      {"Ljavax/net/ssl/BridgedTls;", "write", {"J", "[B", "I", "I"}, "V", TlsNativeWrite},
      {"Ljavax/net/ssl/BridgedTls;", "read", {"J", "I", "I"}, "[B", TlsNativeRead},
      {"Ljavax/net/ssl/BridgedTls;", "close", {"J"}, "V", TlsNativeClose},
  };
  auto bind = [&](VMethod& m) {
    if (m.code != nullptr || m.native_fn != nullptr || m.jni_native_fn != nullptr) return;
    if ((m.access_flags & 0x100) == 0) return;  // 仅 native 方法
    for (const Known& k : kKnown) {
      if (klass->descriptor != k.cls || m.name != k.name || m.return_type != k.ret) continue;
      if (m.param_types != k.params) continue;
      m.native_fn = ToIntrinsicSlot(k.fn);
      return;
    }
  };
  for (VMethod& m : klass->direct_methods) bind(m);
  for (VMethod& m : klass->virtual_methods) bind(m);

  // ---- P0 翻转覆写面（lib 方法体 → VM native 面；native_fn 优先于字节码派发）----
  // ① VarHandle 形态原子类：java.lang.invoke 后置 → CAS/内存序方法整体覆写到
  //    VM 原子面（<clinit> findVarHandle 覆写为空）；get/set 字段直读与
  //    update*/accumulate* 组合面留 lib 本体（经覆写 CAS 原语递归调用）。
  // ② ServiceLoader JPMS 面：checkCaller（uses 声明检查）/ModuleServicesLookupIterator.
  //    iteratorFor（模块服务目录）——无 JPMS 语义，覆写为 null-op / 恒空。
  // ③ LazyClassPathLookupIterator.parse：资源读取 IO 接缝（URL→session zip 抽取）。
  // ④ URL.toExternalForm：handler 查找面后置（MakeURL 字段约定还原）。
  auto override_bind = [&](VMethod& m) {
    if (m.native_fn != nullptr || m.jni_native_fn != nullptr) return;
    const std::string& d = klass->descriptor;
    IntrinsicFn fn = nullptr;
    if (d == "Ljava/net/URL;") {
      if (m.name == "toExternalForm" && m.return_type == "Ljava/lang/String;") fn = URLToExternalForm;
    } else if (d == "Ljava/util/ServiceLoader;") {
      if (m.name == "checkCaller") fn = NoopClinit;
    } else if (d == "Ljava/util/ServiceLoader$ModuleServicesLookupIterator;") {
      if (m.name == "iteratorFor") fn = SLModuleIteratorFor;
    } else if (d == "Ljava/util/ServiceLoader$LazyClassPathLookupIterator;") {
      if (m.name == "parse") fn = SLParseConfig;
    } else if (d == "Ljava/util/concurrent/atomic/AtomicBoolean;") {
      if (m.name == "<clinit>") {
        fn = NoopClinit;
      } else if (m.name == "get" || m.name == "getPlain" || m.name == "getOpaque" ||
                 m.name == "getAcquire") {
        fn = AtomicBoolGet;
      } else if (m.name == "set" || m.name == "setPlain" || m.name == "setOpaque" ||
                 m.name == "setRelease" || m.name == "lazySet") {
        fn = AtomicBoolSet;
      } else if (m.name == "getAndSet") {
        fn = AtomicBoolGetAndSet;
      } else if (m.name == "compareAndSet" || m.name.rfind("weakCompareAndSet", 0) == 0) {
        fn = AtomicBoolCompareAndSet;
      } else if (m.name.rfind("compareAndExchange", 0) == 0) {
        fn = AtomicBoolCompareAndExchange;
      }
    } else if (d == "Ljava/util/concurrent/atomic/AtomicReference;") {
      if (m.name == "<clinit>") {
        fn = NoopClinit;
      } else if (m.name == "get" || m.name == "getPlain" || m.name == "getOpaque" ||
                 m.name == "getAcquire") {
        fn = AtomicRefGet;
      } else if (m.name == "set" || m.name == "setPlain" || m.name == "setOpaque" ||
                 m.name == "setRelease" || m.name == "lazySet") {
        fn = AtomicRefSet;
      } else if (m.name == "getAndSet") {
        fn = AtomicRefGetAndSet;
      } else if (m.name == "compareAndSet" || m.name.rfind("weakCompareAndSet", 0) == 0) {
        fn = AtomicRefCompareAndSet;
      } else if (m.name.rfind("compareAndExchange", 0) == 0) {
        fn = AtomicRefCompareAndExchange;
      }
    } else if (d == "Landroid/view/Window;") {
      // 窗口系统归桥接层：decor View 由 VM 承接（Activity.getWindow 落点）
      if (m.name == "getDecorView" && m.return_type == "Landroid/view/View;") {
        fn = WindowGetDecorView;
      } else if (m.name == "setContentView" && m.return_type == "V") {
        fn = WindowSetContentView;
      } else if (m.name == "getCallback" &&
                 m.return_type == "Landroid/view/Window$Callback;") {
        // AOSP：attach 时 Window.setCallback(activity)；AppCompat 断言非空
        fn = WindowGetCallback;
      } else if (m.name == "setCallback" && m.return_type == "V") {
        fn = WindowSetCallback;
      } else if (m.name == "setDecorFitsSystemWindows" && m.return_type == "V") {
        fn = WindowSetDecorFitsSystemWindows;
      } else if (m.name == "setStatusBarColor" && m.return_type == "V") {
        fn = WindowSetStatusBarColor;
      } else if (m.name == "setNavigationBarColor" && m.return_type == "V") {
        fn = WindowSetStatusBarColor;
      } else if (m.name == "addFlags" && m.return_type == "V") {
        fn = WindowSetStatusBarColor;  // 同形空转（无宿主窗口面）
      } else if (m.name == "clearFlags" && m.return_type == "V") {
        fn = WindowSetStatusBarColor;
      } else if (m.name == "setSoftInputMode" && m.return_type == "V") {
        fn = WindowSetStatusBarColor;
      } else if (m.name == "findViewById" && m.return_type == "Landroid/view/View;") {
        fn = WindowFindViewById;
      }
    } else if (d == "Ljava/util/concurrent/atomic/AtomicReferenceArray;") {
      if (m.name == "<clinit>") {
        fn = NoopClinit;
      } else if (m.name == "get" || m.name == "getPlain" || m.name == "getOpaque" ||
                 m.name == "getAcquire") {
        fn = AtomicArrGet;
      } else if (m.name == "set" || m.name == "setPlain" || m.name == "setOpaque" ||
                 m.name == "setRelease" || m.name == "lazySet") {
        fn = AtomicArrSet;
      } else if (m.name == "getAndSet") {
        fn = AtomicArrGetAndSet;
      } else if (m.name == "compareAndSet" || m.name.rfind("weakCompareAndSet", 0) == 0) {
        fn = AtomicArrCompareAndSet;
      } else if (m.name.rfind("compareAndExchange", 0) == 0) {
        fn = AtomicArrCompareAndExchange;
      }
    }
    if (fn != nullptr) m.native_fn = ToIntrinsicSlot(fn);
  };
  for (VMethod& m : klass->direct_methods) override_bind(m);
  for (VMethod& m : klass->virtual_methods) override_bind(m);
}

// ---- 单一事实源清单（core/builtin_classes.def，X 宏展开）----
// 内建类集由 builtin_classes.def 权威表达：注册面（RegisterBuiltins/
// RegisterFrameworkBuiltins）、构建派生（runtime-libs 打包排除集）、校验工具
// （tools/runtime_coverage_tests.sh）同源读取，废除 allowlist/excluded 双表单。
const std::vector<std::string>& CoreBuiltinClassManifest() {
  static const std::vector<std::string> kManifest = {
#define DEXVM_CORE_BUILTIN_CLASS(descriptor) descriptor,
#define DEXVM_FRAMEWORK_BUILTIN_CLASS(descriptor)
#include "core/builtin_classes.def"
#undef DEXVM_CORE_BUILTIN_CLASS
#undef DEXVM_FRAMEWORK_BUILTIN_CLASS
  };
  return kManifest;
}

const std::vector<std::string>& FrameworkBuiltinClassManifest() {
  static const std::vector<std::string> kManifest = {
#define DEXVM_CORE_BUILTIN_CLASS(descriptor)
#define DEXVM_FRAMEWORK_BUILTIN_CLASS(descriptor) descriptor,
#include "core/builtin_classes.def"
#undef DEXVM_CORE_BUILTIN_CLASS
#undef DEXVM_FRAMEWORK_BUILTIN_CLASS
  };
  return kManifest;
}

const std::vector<std::string>& BuiltinClassManifest() {
  static const std::vector<std::string> kManifest = [] {
    std::vector<std::string> all = CoreBuiltinClassManifest();
    const std::vector<std::string>& fw = FrameworkBuiltinClassManifest();
    all.insert(all.end(), fw.begin(), fw.end());
    std::sort(all.begin(), all.end());
    return all;
  }();
  return kManifest;
}

bool VerifyBuiltinClassSet(const std::vector<std::string>& registered,
                           const std::vector<std::string>& manifest, std::string* error) {
  const std::set<std::string> reg(registered.begin(), registered.end());
  const std::set<std::string> man(manifest.begin(), manifest.end());
  std::string out;
  for (const std::string& d : man) {
    if (reg.count(d) == 0) {
      out += "  manifest entry never registered: " + d + "\n";
    }
  }
  for (const std::string& d : reg) {
    if (man.count(d) == 0) {
      out += "  registered class missing from manifest: " + d + "\n";
    }
  }
  if (!out.empty()) {
    if (error != nullptr) *error = out;
    return false;
  }
  return true;
}

void AssertBuiltinRegistration(const std::vector<VClass*>& all,
                               const std::vector<std::string>& manifest, const char* registrar) {
  std::vector<std::string> registered;
  registered.reserve(all.size());
  for (VClass* k : all) {
    if (k != nullptr) registered.push_back(k->descriptor);
  }
  // 同描述符重复登记（同名多 VClass）：致命（后建覆盖前建会静默换超类/布局——
  // StackOverflowError super=Error 事故根因）；集合口径比对之外的强一致断言。
  std::set<std::string> seen;
  for (const std::string& d : registered) {
    if (!seen.insert(d).second) {
      std::fprintf(stderr, "builtin manifest mismatch (%s): %s registered more than once\n",
                   registrar, d.c_str());
      std::fflush(stderr);
      std::abort();
    }
  }
  std::string error;
  if (!VerifyBuiltinClassSet(registered, manifest, &error)) {
    std::fprintf(stderr,
                 "builtin manifest mismatch (%s): registered set != builtin_classes.def\n%s",
                 registrar, error.c_str());
    std::fflush(stderr);
    std::abort();
  }
}

void RegisterBuiltins(ClassLinker* linker) {
  if (linker == nullptr) return;
  // 幂等：内置类已登记（或 boot dex 已定义）则跳过
  if (linker->FindClass("Ljava/lang/Object;") != nullptr) return;

  std::vector<VClass*> all;

  // java.lang.Class（元类；klass 自指）
  VClass* class_class =
      MakeClass(linker, &all, "Ljava/lang/Class;", nullptr, 0, kAccPublic);
  // java.lang.Object
  VClass* object = MakeClass(linker, &all, "Ljava/lang/Object;", nullptr, 0, kAccPublic);
  // java.lang.String（VString 布局：头 + length + chars；AllocString 挂此类）
  VClass* string = MakeClass(linker, &all, "Ljava/lang/String;", object,
                             static_cast<uint32_t>(sizeof(VString) - sizeof(ObjHeader)),
                             kAccPublic | kAccFinal);
  // java.lang.Throwable（detailMessage@0 + cause@8 实例字段；异常壳 <init> 变体
  // = VM 抛出面/构造面最小集：()/(String)/(Throwable)/(String,Throwable)）
  VClass* throwable =
      MakeClass(linker, &all, "Ljava/lang/Throwable;", object, 32, kAccPublic);
  if (throwable != nullptr) {
    VField msg;
    msg.declaring = throwable;
    msg.name = "detailMessage";
    msg.type_descriptor = "Ljava/lang/String;";
    msg.access_flags = kAccProtected;
    msg.offset = 0;
    msg.is_static = false;
    throwable->instance_fields.push_back(std::move(msg));
    VField cause;
    cause.declaring = throwable;
    cause.name = "cause";
    cause.type_descriptor = "Ljava/lang/Throwable;";
    cause.access_flags = kAccProtected;
    cause.offset = 8;
    cause.is_static = false;
    throwable->instance_fields.push_back(std::move(cause));
    // 隐藏槽（OpenJDK Throwable.backtrace/stackTrace/suppressedExceptions 同构；
    // append-only：detailMessage@0/cause@8 契约不动，子类 MakeClass 走
    // object_size=max(super,…) 继承扩容）。
    VField st;
    st.declaring = throwable;
    st.name = "stackTrace";
    st.type_descriptor = "[Ljava/lang/StackTraceElement;";
    st.access_flags = kAccPrivate;
    st.offset = 16;
    st.is_static = false;
    throwable->instance_fields.push_back(std::move(st));
    VField sup;
    sup.declaring = throwable;
    sup.name = "suppressedExceptions";
    sup.type_descriptor = "[Ljava/lang/Throwable;";
    sup.access_flags = kAccPrivate;
    sup.offset = 24;
    sup.is_static = false;
    throwable->instance_fields.push_back(std::move(sup));
  }

  if (class_class != nullptr && object != nullptr) {
    class_class->super_class = object;
    class_class->klass = class_class;
    object->klass = class_class;
    if (string != nullptr) string->klass = class_class;
    if (throwable != nullptr) throwable->klass = class_class;
  }

  if (object != nullptr) {
    AddMethodSkeleton(object, "<init>", {}, "V", kAccPublic | kAccConstructor);
  }
  if (string != nullptr) {
    // String.<init> 骨架（StringFactory 重写，对照 interpreter_common.cc:1225-1256）
    const uint32_t ctor = kAccPublic | kAccConstructor;
    AddMethodSkeleton(string, "<init>", {}, "V", ctor);
    AddMethodSkeleton(string, "<init>", {"Ljava/lang/String;"}, "V", ctor);
    AddMethodSkeleton(string, "<init>", {"[C"}, "V", ctor);
    AddMethodSkeleton(string, "<init>", {"[C", "I", "I"}, "V", ctor);
    AddMethodSkeleton(string, "<init>", {"[B"}, "V", ctor);
    AddMethodSkeleton(string, "<init>", {"[B", "I", "I"}, "V", ctor);
    // Okio Buffer.readString(Charset) / JSON 响应解码热路径（StringFactory 承接）
    AddMethodSkeleton(string, "<init>", {"[B", "Ljava/nio/charset/Charset;"}, "V", ctor);
    AddMethodSkeleton(string, "<init>", {"[B", "I", "I", "Ljava/nio/charset/Charset;"}, "V",
                      ctor);
    AddMethodSkeleton(string, "<init>", {"[B", "Ljava/lang/String;"}, "V", ctor);
    AddMethodSkeleton(string, "<init>", {"[B", "I", "I", "Ljava/lang/String;"}, "V", ctor);
    AddMethodSkeleton(string, "<init>", {"Ljava/lang/StringBuilder;"}, "V", ctor);
    AddMethodSkeleton(string, "<init>", {"Ljava/lang/CharSequence;"}, "V", ctor);
    // java.lang.String lib 面静态契约（OpenJDK String.COMPACT_STRINGS/LATIN1/UTF16）：
    // vendored java.lang（Integer/Long/StringUTF16/StringConcatHelper 等）运行期
    // sget String.COMPACT_STRINGS（编译期非常量、不会内联）——内建 String 布局
    // 需承载该静态槽（NoSuchFieldError 面）。值 = false：内建 VString 存储恒为
    // 2 字节/char（无 latin1 压缩态），vendored 助手走 UTF16 分支（两分支经
    // StringLatin1/StringUTF16.newString 的 char[] 适配均可达）。
    VField cs;
    cs.declaring = string;
    cs.name = "COMPACT_STRINGS";
    cs.type_descriptor = "Z";
    cs.access_flags = kAccStatic | kAccFinal;
    cs.offset = 0;
    cs.is_static = true;
    string->static_fields.push_back(std::move(cs));
    VField lat;
    lat.declaring = string;
    lat.name = "LATIN1";
    lat.type_descriptor = "B";
    lat.access_flags = kAccStatic | kAccFinal;
    lat.offset = 1;
    lat.is_static = true;
    string->static_fields.push_back(std::move(lat));
    VField u16;
    u16.declaring = string;
    u16.name = "UTF16";
    u16.type_descriptor = "B";
    u16.access_flags = kAccStatic | kAccFinal;
    u16.offset = 2;
    u16.is_static = true;
    string->static_fields.push_back(std::move(u16));
    string->static_storage.assign(3, ZeroValue());
    string->static_storage[2].i = 1;  // UTF16 = 1
    // java.base 内部界限面（lib StringUTF16/StringLatin1 编译期引用；语义照抄
    // vendored java/lang/String.java——见 StrCheck* 节）
    const uint32_t stat_pub = kAccPublic | kAccStatic;
    AddIntrinsicMethod(string, "checkOffset", {"I", "I"}, "V", stat_pub, StrCheckOffset);
    AddIntrinsicMethod(string, "checkIndex", {"I", "I"}, "V", stat_pub, StrCheckIndex);
    AddIntrinsicMethod(string, "checkBoundsBeginEnd", {"I", "I", "I"}, "V", stat_pub,
                       StrCheckBoundsBeginEnd);
    AddIntrinsicMethod(string, "checkBoundsOffCount", {"I", "I", "I"}, "V", stat_pub,
                       StrCheckBoundsOffCount);
    AddIntrinsicMethod(string, "coder", {}, "B", kAccPublic, StrCoder);
    AddIntrinsicMethod(string, "getBytes", {"[B", "I", "B"}, "V", kAccPublic,
                       StrGetBytesInternal);
  }
  if (throwable != nullptr) {
    // java.lang.Throwable.<init> 变体（VM 抛出面/构造面最小集）：
    // ()/(String)：detailMessage；(Throwable)/(String,Throwable)：+ cause（lib
    // 异常壳构造链（ServiceConfigurationError(msg,cause) 等）经超类链命中本面）
    const uint32_t ctor = kAccPublic | kAccConstructor;
    AddIntrinsicMethod(throwable, "<init>", {}, "V", ctor, ThrowableInit);
    AddIntrinsicMethod(throwable, "<init>", {"Ljava/lang/String;"}, "V", ctor, ThrowableInitMsg);
    AddIntrinsicMethod(throwable, "<init>", {"Ljava/lang/Throwable;"}, "V", ctor,
                       ThrowableInitCause);
    AddIntrinsicMethod(throwable, "<init>", {"Ljava/lang/String;", "Ljava/lang/Throwable;"},
                       "V", ctor, ThrowableInitMsgCause);
    AddIntrinsicMethod(throwable, "getCause", {}, "Ljava/lang/Throwable;", kAccPublic,
                       ThrowableGetCause);
    AddIntrinsicMethod(throwable, "initCause", {"Ljava/lang/Throwable;"},
                       "Ljava/lang/Throwable;", kAccPublic, ThrowableInitCauseMethod);
  }

  // 异常类层级（detailMessage 布局继承自 Throwable）
  VClass* exception = nullptr;
  VClass* runtime_ex = nullptr;
  VClass* error = nullptr;
  VClass* vm_error = nullptr;
  VClass* linkage = nullptr;
  VClass* ioobe = nullptr;
  if (throwable != nullptr) {
    exception = MakeClass(linker, &all, "Ljava/lang/Exception;", throwable, 16, kAccPublic);
    runtime_ex =
        MakeClass(linker, &all, "Ljava/lang/RuntimeException;", exception, 16, kAccPublic);
    error = MakeClass(linker, &all, "Ljava/lang/Error;", throwable, 16, kAccPublic);
    // Java 层级：VirtualMachineError（abstract）-> Error；OOM/SO/IE/Unknown 之下
    vm_error = MakeClass(linker, &all, "Ljava/lang/VirtualMachineError;", error, 16,
                         kAccPublic | kAccAbstract);
    linkage = MakeClass(linker, &all, "Ljava/lang/LinkageError;", error, 16, kAccPublic);
    ioobe = MakeClass(linker, &all, "Ljava/lang/IndexOutOfBoundsException;", runtime_ex, 16,
                      kAccPublic);
    if (class_class != nullptr) {
      for (VClass* k : {exception, runtime_ex, error, vm_error, linkage, ioobe}) {
        if (k != nullptr) k->klass = class_class;
      }
    }

    struct {
      const char* desc;
      VClass* super;
    } kExceptions[] = {
        {"Ljava/lang/NullPointerException;", runtime_ex},
        {"Ljava/lang/IllegalArgumentException;", runtime_ex},
        {"Ljava/lang/NegativeArraySizeException;", runtime_ex},
        {"Ljava/lang/ArrayIndexOutOfBoundsException;", ioobe},
        {"Ljava/lang/StringIndexOutOfBoundsException;", ioobe},
        {"Ljava/lang/ClassCastException;", runtime_ex},
        {"Ljava/lang/ArithmeticException;", runtime_ex},
        {"Ljava/lang/UnsupportedOperationException;", runtime_ex},
        {"Ljava/lang/ArrayStoreException;", runtime_ex},
        {"Ljava/lang/IllegalMonitorStateException;", runtime_ex},
        {"Ljava/lang/IllegalStateException;", runtime_ex},
        {"Ljava/lang/OutOfMemoryError;", vm_error},
        {"Ljava/lang/InternalError;", vm_error},
        {"Ljava/lang/StackOverflowError;", vm_error},
        {"Ljava/lang/UnknownError;", vm_error},
        {"Ljava/lang/ThreadDeath;", error},
        {"Ljava/lang/ExceptionInInitializerError;", linkage},
        {"Ljava/lang/NoClassDefFoundError;", linkage},
        {"Ljava/lang/NoSuchMethodError;", linkage},
        {"Ljava/lang/NoSuchFieldError;", linkage},
        {"Ljava/lang/AbstractMethodError;", linkage},
        {"Ljava/lang/InstantiationError;", linkage},
    };
    for (const auto& e : kExceptions) {
      VClass* k = MakeClass(linker, &all, e.desc, e.super, 16, kAccPublic);
      if (k != nullptr && class_class != nullptr) k->klass = class_class;
    }
    // M2：Java loadClass 回落与 DEX 打开路径所需异常类
    VClass* roe = MakeClass(linker, &all, "Ljava/lang/ReflectiveOperationException;", exception,
                            16, kAccPublic);
    VClass* cnfe =
        MakeClass(linker, &all, "Ljava/lang/ClassNotFoundException;", roe, 16, kAccPublic);
    VClass* ioe = MakeClass(linker, &all, "Ljava/io/IOException;", exception, 16, kAccPublic);
    if (class_class != nullptr) {
      for (VClass* k : {roe, cnfe, ioe}) {
        if (k != nullptr) k->klass = class_class;
      }
    }
    (void)cnfe;
  }

  // ---- M3：Object/Class/Throwable 方法面 + 接口类 + String/StringBuilder ----
  if (object != nullptr && class_class != nullptr) {
    const uint32_t ctor = kAccPublic | kAccConstructor;
    const uint32_t pub = kAccPublic;
    const uint32_t stat = kAccPublic | kAccStatic;
    const uint32_t pub_final = kAccPublic | kAccFinal;
    (void)ctor;

    // java.lang.Object 元方法（valueOf(Object)/toString 链路 + 常规调用面）
    AddIntrinsicMethod(object, "hashCode", {}, "I", pub, ObjHashCode);
    AddIntrinsicMethod(object, "equals", {"Ljava/lang/Object;"}, "Z", pub, ObjEquals);
    AddIntrinsicMethod(object, "toString", {}, "Ljava/lang/String;", pub, ObjToString);
    AddIntrinsicMethod(object, "getClass", {}, "Ljava/lang/Class;", pub_final, ObjGetClass);
    // clone：数组浅拷贝 / Cloneable 浅拷贝 / 否则 CloneNotSupportedException
    //（数组 clone 为 Java 公共语义——Enum.values() 等编译产物依赖）
    AddIntrinsicMethod(object, "clone", {}, "Ljava/lang/Object;", kAccProtected, ObjClone);
    // M4.4 monitor wait 面（docs 04 章 14.8：wait 释放重入计数挂协程 wait、
    // notify/notifyAll 唤醒 monitor waiter list；LinkedBlockingQueue.take 前置）
    AddIntrinsicMethod(object, "wait", {}, "V", pub_final, ObjWait);
    AddIntrinsicMethod(object, "wait", {"J"}, "V", pub_final, ObjWait);
    AddIntrinsicMethod(object, "wait", {"J", "I"}, "V", pub_final, ObjWait);
    AddIntrinsicMethod(object, "notify", {}, "V", pub_final, ObjNotify);
    AddIntrinsicMethod(object, "notifyAll", {}, "V", pub_final, ObjNotifyAll);

    // java.lang.Class 元方法（const-class 对象即 VClass）
    AddIntrinsicMethod(class_class, "getName", {}, "Ljava/lang/String;", pub, ClassGetName);
    AddIntrinsicMethod(class_class, "toString", {}, "Ljava/lang/String;", pub, ClassToString);
    AddIntrinsicMethod(class_class, "getPrimitiveClass", {"Ljava/lang/String;"},
                       "Ljava/lang/Class;", stat, ClassGetPrimitiveClass);
    // 注解反射面（M4.3；无注解投影，注解解码后置）
    AddIntrinsicMethod(class_class, "getAnnotation", {"Ljava/lang/Class;"},
                       "Ljava/lang/annotation/Annotation;", pub, ClassGetAnnotation);
    AddIntrinsicMethod(class_class, "getDeclaredAnnotation", {"Ljava/lang/Class;"},
                       "Ljava/lang/annotation/Annotation;", pub, ClassGetAnnotation);
    AddIntrinsicMethod(class_class, "getAnnotationsByType", {"Ljava/lang/Class;"},
                       "[Ljava/lang/annotation/Annotation;", pub, ClassGetAnnotations);
    AddIntrinsicMethod(class_class, "getAnnotations", {}, "[Ljava/lang/annotation/Annotation;", pub,
                       ClassGetAnnotations);
    AddIntrinsicMethod(class_class, "getDeclaredAnnotations", {},
                       "[Ljava/lang/annotation/Annotation;", pub, ClassGetAnnotations);
    AddIntrinsicMethod(class_class, "isAnnotationPresent", {"Ljava/lang/Class;"}, "Z", pub,
                       ClassIsAnnotationPresent);

    // Throwable 消息面
    if (throwable != nullptr) {
      AddIntrinsicMethod(throwable, "getMessage", {}, "Ljava/lang/String;", pub,
                         ThrowableGetMessage);
      AddIntrinsicMethod(throwable, "getLocalizedMessage", {}, "Ljava/lang/String;", pub,
                         ThrowableGetLocalizedMessage);
      AddIntrinsicMethod(throwable, "toString", {}, "Ljava/lang/String;", pub, ThrowableToString);
      AddIntrinsicMethod(throwable, "fillInStackTrace", {}, "Ljava/lang/Throwable;", pub,
                         ThrowableFillInStackTrace);
      // Throwable 抛出面（栈/抑制/printStackTrace；zhihu 考卷 messagePump 链）
      AddIntrinsicMethod(throwable, "getStackTrace", {}, "[Ljava/lang/StackTraceElement;", pub,
                         ThrowableGetStackTrace);
      AddIntrinsicMethod(throwable, "setStackTrace", {"[Ljava/lang/StackTraceElement;"}, "V", pub,
                         ThrowableSetStackTrace);
      AddIntrinsicMethod(throwable, "addSuppressed", {"Ljava/lang/Throwable;"}, "V",
                         pub | kAccFinal, ThrowableAddSuppressed);
      AddIntrinsicMethod(throwable, "getSuppressed", {}, "[Ljava/lang/Throwable;",
                         pub | kAccFinal, ThrowableGetSuppressed);
      AddIntrinsicMethod(throwable, "printStackTrace", {}, "V", pub, ThrowablePrintStackTrace);
      AddIntrinsicMethod(throwable, "printStackTrace", {"Ljava/io/PrintStream;"}, "V", pub,
                         ThrowablePrintStackTraceStream);
      AddIntrinsicMethod(throwable, "printStackTrace", {"Ljava/io/PrintWriter;"}, "V", pub,
                         ThrowablePrintStackTraceWriter);
    }

    // P0 翻转：CharSequence/Comparable/Serializable/Annotation 接口类改由
    // runtime.dex 提供（内建注册删除，§31.3 第 5 步）。String/StringBuilder 的
    // 接口向量按描述符延迟补挂（接口类首次 DefineClass 时挂载，见
    // ClassLinker::AddPendingBuiltinInterface）。
    if (string != nullptr) {
      linker->AddPendingBuiltinInterface("Ljava/lang/String;", "Ljava/lang/CharSequence;");
      linker->AddPendingBuiltinInterface("Ljava/lang/String;", "Ljava/lang/Comparable;");
      linker->AddPendingBuiltinInterface("Ljava/lang/String;", "Ljava/io/Serializable;");
    }

    // java.lang.String 方法面（intrinsic；Android java.lang.String 规格）
    if (string != nullptr) {
      AddIntrinsicMethod(string, "length", {}, "I", pub_final, StrLength);
      AddIntrinsicMethod(string, "isEmpty", {}, "Z", pub, StrIsEmpty);
      AddIntrinsicMethod(string, "toString", {}, "Ljava/lang/String;", pub, StrToString);
      AddIntrinsicMethod(string, "charAt", {"I"}, "C", pub_final, StrCharAt);
      AddIntrinsicMethod(string, "codePointAt", {"I"}, "I", pub, StrCodePointAt);
      AddIntrinsicMethod(string, "codePointBefore", {"I"}, "I", pub, StrCodePointBefore);
      AddIntrinsicMethod(string, "codePointCount", {"I", "I"}, "I", pub, StrCodePointCount);
      AddIntrinsicMethod(string, "offsetByCodePoints", {"I", "I"}, "I", pub,
                         StrOffsetByCodePoints);
      AddIntrinsicMethod(string, "hashCode", {}, "I", pub, StrHashCode);
      AddIntrinsicMethod(string, "equals", {"Ljava/lang/Object;"}, "Z", pub_final, StrEquals);
      AddIntrinsicMethod(string, "equalsIgnoreCase", {"Ljava/lang/String;"}, "Z", pub,
                         StrEqualsIgnoreCase);
      AddIntrinsicMethod(string, "compareTo", {"Ljava/lang/String;"}, "I", pub_final,
                         StrCompareTo);
      // Comparable<String> 桥（javac 合成 compareTo(Object)；Enum 同款桥注册）
      AddIntrinsicMethod(string, "compareTo", {"Ljava/lang/Object;"}, "I", pub_final,
                         StrCompareToObject);
      AddIntrinsicMethod(string, "compareToIgnoreCase", {"Ljava/lang/String;"}, "I", pub,
                         StrCompareToIgnoreCase);
      AddIntrinsicMethod(string, "concat", {"Ljava/lang/String;"}, "Ljava/lang/String;", pub_final,
                         StrConcat);
      AddIntrinsicMethod(string, "contains", {"Ljava/lang/CharSequence;"}, "Z", pub, StrContains);
      AddIntrinsicMethod(string, "contentEquals", {"Ljava/lang/CharSequence;"}, "Z", pub,
                         StrContentEquals);
      AddIntrinsicMethod(string, "startsWith", {"Ljava/lang/String;"}, "Z", pub, StrStartsWith);
      AddIntrinsicMethod(string, "startsWith", {"Ljava/lang/String;", "I"}, "Z", pub,
                         StrStartsWith);
      AddIntrinsicMethod(string, "endsWith", {"Ljava/lang/String;"}, "Z", pub, StrEndsWith);
      AddIntrinsicMethod(string, "regionMatches", {"I", "Ljava/lang/String;", "I", "I"}, "Z", pub,
                         StrRegionMatches);
      AddIntrinsicMethod(string, "regionMatches",
                         {"Z", "I", "Ljava/lang/String;", "I", "I"}, "Z", pub, StrRegionMatches);
      AddIntrinsicMethod(string, "indexOf", {"I"}, "I", pub, StrIndexOfChar);
      AddIntrinsicMethod(string, "indexOf", {"I", "I"}, "I", pub, StrIndexOfChar);
      AddIntrinsicMethod(string, "indexOf", {"Ljava/lang/String;"}, "I", pub, StrIndexOfStr);
      AddIntrinsicMethod(string, "indexOf", {"Ljava/lang/String;", "I"}, "I", pub, StrIndexOfStr);
      AddIntrinsicMethod(string, "lastIndexOf", {"I"}, "I", pub, StrLastIndexOfChar);
      AddIntrinsicMethod(string, "lastIndexOf", {"I", "I"}, "I", pub, StrLastIndexOfChar);
      AddIntrinsicMethod(string, "lastIndexOf", {"Ljava/lang/String;"}, "I", pub,
                         StrLastIndexOfStr);
      AddIntrinsicMethod(string, "lastIndexOf", {"Ljava/lang/String;", "I"}, "I", pub,
                         StrLastIndexOfStr);
      AddIntrinsicMethod(string, "replace", {"C", "C"}, "Ljava/lang/String;", pub, StrReplaceChar);
      AddIntrinsicMethod(string, "replace",
                         {"Ljava/lang/CharSequence;", "Ljava/lang/CharSequence;"},
                         "Ljava/lang/String;", pub, StrReplaceSeq);
      AddIntrinsicMethod(string, "substring", {"I"}, "Ljava/lang/String;", pub_final,
                         StrSubstring1);
      AddIntrinsicMethod(string, "substring", {"I", "I"}, "Ljava/lang/String;", pub_final,
                         StrSubstring2);
      AddIntrinsicMethod(string, "subSequence", {"I", "I"}, "Ljava/lang/CharSequence;", pub,
                         StrSubSequence);
      AddIntrinsicMethod(string, "toLowerCase", {}, "Ljava/lang/String;", pub, StrToLowerCase);
      AddIntrinsicMethod(string, "toUpperCase", {}, "Ljava/lang/String;", pub, StrToUpperCase);
      // Locale 参数版（§31.7 小步：String 复杂面拆分缝未建）：委托无参版语义
      // （SimpleLower/SimpleUpper）；locale 敏感映射（tr/az 等）+ full SpecialCasing
      // 随拆分缝后置——报告注明近似。
      AddIntrinsicMethod(string, "toLowerCase", {"Ljava/util/Locale;"}, "Ljava/lang/String;", pub,
                         StrToLowerCase);
      AddIntrinsicMethod(string, "toUpperCase", {"Ljava/util/Locale;"}, "Ljava/lang/String;", pub,
                         StrToUpperCase);
      AddIntrinsicMethod(string, "trim", {}, "Ljava/lang/String;", pub, StrTrim);
      // split(regex) 面（zonfig TarsConstants <clinit> 链;委托 lib java.util.regex）
      AddIntrinsicMethod(string, "split", {"Ljava/lang/String;"}, "[Ljava/lang/String;", pub,
                         StrSplit);
      AddIntrinsicMethod(string, "split", {"Ljava/lang/String;", "I"}, "[Ljava/lang/String;", pub,
                         StrSplitLimit);
      // matches/replaceAll(regex) 面（同 split 先例薄委托 lib java.util.regex）
      AddIntrinsicMethod(string, "matches", {"Ljava/lang/String;"}, "Z", pub, StrMatches);
      AddIntrinsicMethod(string, "replaceAll", {"Ljava/lang/String;", "Ljava/lang/String;"},
                         "Ljava/lang/String;", pub, StrReplaceAll);
      AddIntrinsicMethod(string, "toCharArray", {}, "[C", pub, StrToCharArray);
      AddIntrinsicMethod(string, "getBytes", {}, "[B", pub, StrGetBytes);
      AddIntrinsicMethod(string, "getBytes", {"Ljava/lang/String;"}, "[B", pub, StrGetBytes);
      AddIntrinsicMethod(string, "getBytes", {"Ljava/nio/charset/Charset;"}, "[B", pub,
                         StrGetBytesCharset);
      AddIntrinsicMethod(string, "getChars", {"I", "I", "[C", "I"}, "V", pub, StrGetChars);
      AddIntrinsicMethod(string, "valueOf", {"Z"}, "Ljava/lang/String;", stat, StrValueOfBool);
      AddIntrinsicMethod(string, "valueOf", {"C"}, "Ljava/lang/String;", stat, StrValueOfChar);
      AddIntrinsicMethod(string, "valueOf", {"I"}, "Ljava/lang/String;", stat, StrValueOfInt);
      AddIntrinsicMethod(string, "valueOf", {"J"}, "Ljava/lang/String;", stat, StrValueOfLong);
      AddIntrinsicMethod(string, "valueOf", {"F"}, "Ljava/lang/String;", stat, StrValueOfFloat);
      AddIntrinsicMethod(string, "valueOf", {"D"}, "Ljava/lang/String;", stat, StrValueOfDouble);
      AddIntrinsicMethod(string, "valueOf", {"[C"}, "Ljava/lang/String;", stat, StrValueOfChars);
      AddIntrinsicMethod(string, "valueOf", {"[C", "I", "I"}, "Ljava/lang/String;", stat,
                         StrValueOfCharsRange);
      AddIntrinsicMethod(string, "valueOf", {"Ljava/lang/Object;"}, "Ljava/lang/String;", stat,
                         StrValueOfObject);
      AddIntrinsicMethod(string, "copyValueOf", {"[C"}, "Ljava/lang/String;", stat,
                         StrValueOfChars);
      AddIntrinsicMethod(string, "copyValueOf", {"[C", "I", "I"}, "Ljava/lang/String;", stat,
                         StrValueOfCharsRange);
      AddIntrinsicMethod(string, "format", {"Ljava/lang/String;", "[Ljava/lang/Object;"},
                         "Ljava/lang/String;", stat, StrFormat);
      AddIntrinsicMethod(string, "format",
                         {"Ljava/util/Locale;", "Ljava/lang/String;", "[Ljava/lang/Object;"},
                         "Ljava/lang/String;", stat, StrFormat);
      AddIntrinsicMethod(string, "intern", {}, "Ljava/lang/String;", pub_final, StrIntern);
    }

    // java.lang.StringBuilder（AbstractStringBuilder 语义：char[] value + int count；
    // 全部方法 intrinsic，公开 API 面按 Android java.lang.StringBuilder 落地）
    VClass* sb = MakeClass(linker, &all, "Ljava/lang/StringBuilder;", object, 16,
                           kAccPublic | kAccFinal);
    if (sb != nullptr && class_class != nullptr) {
      sb->klass = class_class;
      // 接口向量延迟补挂（P0 翻转：接口类由 runtime.dex 提供）
      linker->AddPendingBuiltinInterface("Ljava/lang/StringBuilder;", "Ljava/lang/CharSequence;");
      linker->AddPendingBuiltinInterface("Ljava/lang/StringBuilder;", "Ljava/lang/Comparable;");
      linker->AddPendingBuiltinInterface("Ljava/lang/StringBuilder;", "Ljava/io/Serializable;");
      VField value;
      value.declaring = sb;
      value.name = "value";
      value.type_descriptor = "[C";
      value.access_flags = 0x10002;  // private
      value.offset = 0;
      value.is_static = false;
      sb->instance_fields.push_back(std::move(value));
      VField count;
      count.declaring = sb;
      count.name = "count";
      count.type_descriptor = "I";
      count.access_flags = 0x10002;
      count.offset = 8;
      count.is_static = false;
      sb->instance_fields.push_back(std::move(count));

      const uint32_t sbctor = kAccPublic | kAccConstructor;
      AddIntrinsicMethod(sb, "<init>", {}, "V", sbctor, SBInit);
      AddIntrinsicMethod(sb, "<init>", {"I"}, "V", sbctor, SBInitCap);
      AddIntrinsicMethod(sb, "<init>", {"Ljava/lang/String;"}, "V", sbctor, SBInitStr);
      AddIntrinsicMethod(sb, "<init>", {"Ljava/lang/CharSequence;"}, "V", sbctor, SBInitCS);
      AddIntrinsicMethod(sb, "append", {"Ljava/lang/Object;"}, "Ljava/lang/StringBuilder;", pub,
                         SBAppendObj);
      AddIntrinsicMethod(sb, "append", {"Ljava/lang/String;"}, "Ljava/lang/StringBuilder;", pub,
                         SBAppendStr);
      AddIntrinsicMethod(sb, "append", {"Ljava/lang/StringBuffer;"},
                         "Ljava/lang/StringBuilder;", pub, SBAppendCS);
      AddIntrinsicMethod(sb, "append", {"Ljava/lang/CharSequence;"},
                         "Ljava/lang/StringBuilder;", pub, SBAppendCS);
      AddIntrinsicMethod(sb, "append", {"[C"}, "Ljava/lang/StringBuilder;", pub,
                         SBAppendCharsArg);
      AddIntrinsicMethod(sb, "append", {"[C", "I", "I"}, "Ljava/lang/StringBuilder;", pub,
                         SBAppendCharsRange);
      AddIntrinsicMethod(sb, "append", {"Ljava/lang/CharSequence;", "I", "I"},
                         "Ljava/lang/StringBuilder;", pub, SBAppendCSRange);
      AddIntrinsicMethod(sb, "append", {"Z"}, "Ljava/lang/StringBuilder;", pub, SBAppendBool);
      AddIntrinsicMethod(sb, "append", {"C"}, "Ljava/lang/StringBuilder;", pub, SBAppendChar);
      AddIntrinsicMethod(sb, "append", {"I"}, "Ljava/lang/StringBuilder;", pub, SBAppendInt);
      AddIntrinsicMethod(sb, "append", {"J"}, "Ljava/lang/StringBuilder;", pub, SBAppendLong);
      AddIntrinsicMethod(sb, "append", {"F"}, "Ljava/lang/StringBuilder;", pub, SBAppendFloat);
      AddIntrinsicMethod(sb, "append", {"D"}, "Ljava/lang/StringBuilder;", pub, SBAppendDouble);
      AddIntrinsicMethod(sb, "appendCodePoint", {"I"}, "Ljava/lang/StringBuilder;", pub,
                         SBAppendCodePoint);
      AddIntrinsicMethod(sb, "toString", {}, "Ljava/lang/String;", pub, SBToString);
      AddIntrinsicMethod(sb, "length", {}, "I", pub, SBLength);
      AddIntrinsicMethod(sb, "capacity", {}, "I", pub, SBCapacity);
      AddIntrinsicMethod(sb, "ensureCapacity", {"I"}, "V", pub, SBEnsureCapacity);
      AddIntrinsicMethod(sb, "trimToSize", {}, "V", pub, SBTrimToSize);
      AddIntrinsicMethod(sb, "charAt", {"I"}, "C", pub, SBCharAt);
      AddIntrinsicMethod(sb, "codePointAt", {"I"}, "I", pub, SBCodePointAt);
      AddIntrinsicMethod(sb, "codePointBefore", {"I"}, "I", pub, SBCodePointBefore);
      AddIntrinsicMethod(sb, "codePointCount", {"I", "I"}, "I", pub, SBCodePointCount);
      AddIntrinsicMethod(sb, "offsetByCodePoints", {"I", "I"}, "I", pub, SBOffsetByCodePoints);
      AddIntrinsicMethod(sb, "setCharAt", {"I", "C"}, "V", pub, SBSetCharAt);
      AddIntrinsicMethod(sb, "setLength", {"I"}, "V", pub, SBSetLength);
      AddIntrinsicMethod(sb, "delete", {"I", "I"}, "Ljava/lang/StringBuilder;", pub, SBDelete);
      AddIntrinsicMethod(sb, "deleteCharAt", {"I"}, "Ljava/lang/StringBuilder;", pub,
                         SBDeleteCharAt);
      AddIntrinsicMethod(sb, "replace", {"I", "I", "Ljava/lang/String;"},
                         "Ljava/lang/StringBuilder;", pub, SBReplace);
      AddIntrinsicMethod(sb, "insert", {"I", "Ljava/lang/Object;"}, "Ljava/lang/StringBuilder;",
                         pub, SBInsertStr);
      AddIntrinsicMethod(sb, "insert", {"I", "Ljava/lang/String;"}, "Ljava/lang/StringBuilder;",
                         pub, SBInsertStr);
      AddIntrinsicMethod(sb, "insert", {"I", "Ljava/lang/CharSequence;"},
                         "Ljava/lang/StringBuilder;", pub, SBInsertStr);
      AddIntrinsicMethod(sb, "insert", {"I", "[C"}, "Ljava/lang/StringBuilder;", pub,
                         SBInsertCharsArg);
      AddIntrinsicMethod(sb, "insert", {"I", "[C", "I", "I"}, "Ljava/lang/StringBuilder;", pub,
                         SBInsertCharsRange);
      AddIntrinsicMethod(sb, "insert", {"I", "Ljava/lang/CharSequence;", "I", "I"},
                         "Ljava/lang/StringBuilder;", pub, SBInsertCSRange);
      AddIntrinsicMethod(sb, "insert", {"I", "Z"}, "Ljava/lang/StringBuilder;", pub,
                         SBInsertBool);
      AddIntrinsicMethod(sb, "insert", {"I", "C"}, "Ljava/lang/StringBuilder;", pub,
                         SBInsertChar);
      AddIntrinsicMethod(sb, "insert", {"I", "I"}, "Ljava/lang/StringBuilder;", pub,
                         SBInsertNum);
      AddIntrinsicMethod(sb, "insert", {"I", "J"}, "Ljava/lang/StringBuilder;", pub,
                         SBInsertLong);
      AddIntrinsicMethod(sb, "insert", {"I", "F"}, "Ljava/lang/StringBuilder;", pub,
                         SBInsertFloat);
      AddIntrinsicMethod(sb, "insert", {"I", "D"}, "Ljava/lang/StringBuilder;", pub,
                         SBInsertDouble);
      AddIntrinsicMethod(sb, "reverse", {}, "Ljava/lang/StringBuilder;", pub, SBReverse);
      AddIntrinsicMethod(sb, "substring", {"I"}, "Ljava/lang/String;", pub, SBSubstring1);
      AddIntrinsicMethod(sb, "substring", {"I", "I"}, "Ljava/lang/String;", pub, SBSubstring2);
      AddIntrinsicMethod(sb, "subSequence", {"I", "I"}, "Ljava/lang/CharSequence;", pub,
                         SBSubSequence);
      AddIntrinsicMethod(sb, "indexOf", {"Ljava/lang/String;"}, "I", pub, SBIndexOfStr);
      AddIntrinsicMethod(sb, "indexOf", {"Ljava/lang/String;", "I"}, "I", pub, SBIndexOfStr);
      AddIntrinsicMethod(sb, "indexOf", {"I"}, "I", pub, SBIndexOfChar);
      AddIntrinsicMethod(sb, "indexOf", {"I", "I"}, "I", pub, SBIndexOfChar);
      AddIntrinsicMethod(sb, "lastIndexOf", {"Ljava/lang/String;"}, "I", pub, SBLastIndexOfStr);
      AddIntrinsicMethod(sb, "lastIndexOf", {"Ljava/lang/String;", "I"}, "I", pub,
                         SBLastIndexOfStr);
      AddIntrinsicMethod(sb, "lastIndexOf", {"I"}, "I", pub, SBLastIndexOfChar);
      AddIntrinsicMethod(sb, "lastIndexOf", {"I", "I"}, "I", pub, SBLastIndexOfChar);
      AddIntrinsicMethod(sb, "getChars", {"I", "I", "[C", "I"}, "V", pub, SBGetChars);
      AddIntrinsicMethod(sb, "compareTo", {"Ljava/lang/StringBuilder;"}, "I", pub, SBCompareTo);
      // Appendable/Comparable 协变桥（javac 合成形：接口擦除返回/参数；
      // 与 String/Enum 的 compareTo(Object) 桥同款）。实现体复用同语义 intrinsic。
      AddIntrinsicMethod(sb, "append", {"C"}, "Ljava/lang/Appendable;", pub, SBAppendChar);
      AddIntrinsicMethod(sb, "append", {"Ljava/lang/CharSequence;"},
                         "Ljava/lang/Appendable;", pub, SBAppendCS);
      AddIntrinsicMethod(sb, "append", {"Ljava/lang/CharSequence;", "I", "I"},
                         "Ljava/lang/Appendable;", pub, SBAppendCSRange);
      AddIntrinsicMethod(sb, "compareTo", {"Ljava/lang/Object;"}, "I", pub, SBCompareToObject);
    }
  }

  // ---- M2：ClassLoader 全家 + dalvik.system.DexFile（intrinsic，docs 14 章 28.1/28.2）----
  if (object != nullptr && class_class != nullptr) {
    const uint32_t ctor = kAccPublic | kAccConstructor;
    const uint32_t stat = kAccPublic | kAccStatic;

    // P0 翻转：java/nio/ByteBuffer 空骨架内建注册删除（解除对 lib NIO 的遮蔽）。
    // 堆 buffer 读取接缝按 lib ByteBuffer 布局字段（hb/offset/position，见
    // ExtractDexBytes）；direct buffer 走 JNI GetDirectBufferAddress（后置）。
    // java/lang/ClassLoader：loadClass 双亲委派模板 + findClass 骨架（intrinsic）
    VClass* cl = MakeClass(linker, &all, "Ljava/lang/ClassLoader;", object, 0, kAccPublic);
    if (cl != nullptr) {
      AddIntrinsicMethod(cl, "<init>", {}, "V", ctor, CLInit);
      AddIntrinsicMethod(cl, "<init>", {"Ljava/lang/ClassLoader;"}, "V", ctor, CLInit);
      AddIntrinsicMethod(cl, "loadClass", {"Ljava/lang/String;"}, "Ljava/lang/Class;",
                         kAccPublic, CLLoadClass);
      AddIntrinsicMethod(cl, "findClass", {"Ljava/lang/String;"}, "Ljava/lang/Class;",
                         kAccProtected, CLFindClass);
    }

    // dalvik/system/BaseDexClassLoader：DexPathList（Element=DexFile[]+nativeLibraryDir）
    VClass* bdcl = MakeClass(linker, &all, "Ldalvik/system/BaseDexClassLoader;", cl, 0, kAccPublic);
    if (bdcl != nullptr) {
      AddIntrinsicMethod(bdcl, "<init>",
                         {"Ljava/lang/String;", "Ljava/lang/String;", "Ljava/lang/String;",
                          "Ljava/lang/ClassLoader;"},
                         "V", ctor, BDCLInit);
      AddIntrinsicMethod(bdcl, "findClass", {"Ljava/lang/String;"}, "Ljava/lang/Class;",
                         kAccProtected, CLFindClass);
    }

    // dalvik/system/PathClassLoader（只读已装路径，查找序同 DexClassLoader）
    VClass* pcl = MakeClass(linker, &all, "Ldalvik/system/PathClassLoader;", bdcl, 0, kAccPublic);
    if (pcl != nullptr) {
      AddIntrinsicMethod(pcl, "<init>",
                         {"Ljava/lang/String;", "Ljava/lang/ClassLoader;"}, "V", ctor, PCLInit1);
      AddIntrinsicMethod(pcl, "<init>",
                         {"Ljava/lang/String;", "Ljava/lang/String;", "Ljava/lang/ClassLoader;"},
                         "V", ctor, PCLInit2);
    }

    // dalvik/system/DexClassLoader（dexPath/optDir/libPath/parent → LoaderState）
    VClass* dcl = MakeClass(linker, &all, "Ldalvik/system/DexClassLoader;", bdcl, 0, kAccPublic);
    if (dcl != nullptr) {
      AddIntrinsicMethod(dcl, "<init>",
                         {"Ljava/lang/String;", "Ljava/lang/String;", "Ljava/lang/String;",
                          "Ljava/lang/ClassLoader;"},
                         "V", ctor, BDCLInit);
    }

    // dalvik/system/InMemoryDexClassLoader（ByteBuffer/byte[] → dex 字节）
    VClass* imcl =
        MakeClass(linker, &all, "Ldalvik/system/InMemoryDexClassLoader;", bdcl, 0, kAccPublic);
    if (imcl != nullptr) {
      AddIntrinsicMethod(imcl, "<init>",
                         {"Ljava/nio/ByteBuffer;", "Ljava/lang/ClassLoader;"}, "V", ctor,
                         IMCLInit);
      AddIntrinsicMethod(imcl, "<init>", {"[B", "Ljava/lang/ClassLoader;"}, "V", ctor, IMCLInit);
      AddIntrinsicMethod(imcl, "<init>", {"[[B", "Ljava/lang/ClassLoader;"}, "V", ctor, IMCLInit);
    }

    // dalvik/system/DexFile：cookie 语义（openDexFileNative/defineClassNative/
    // loadClass/getClassNameList/closeDexFile + openDexFile/loadDex 便捷入口）
    VClass* dex_file = MakeClass(linker, &all, "Ldalvik/system/DexFile;", object, 8, kAccPublic);
    if (dex_file != nullptr) {
      AddMethodSkeleton(dex_file, "<init>", {}, "V", ctor);
      AddIntrinsicMethod(dex_file, "openDexFileNative",
                         {"Ljava/lang/String;", "Ljava/lang/String;", "I"}, "J", stat,
                         DexOpenNative);
      AddIntrinsicMethod(dex_file, "openDexFileNative", {"[B"}, "J", stat, DexOpenNativeBytes);
      AddIntrinsicMethod(dex_file, "openDexFileNative", {"[[B"}, "J", stat, DexOpenNativeBytes);
      AddIntrinsicMethod(dex_file, "defineClassNative",
                         {"Ljava/lang/String;", "Ljava/lang/ClassLoader;", "J",
                          "Ldalvik/system/DexFile;"},
                         "Ljava/lang/Class;", stat, DexDefineClassNative);
      AddIntrinsicMethod(dex_file, "loadClass",
                         {"Ljava/lang/String;", "Ljava/lang/ClassLoader;"}, "Ljava/lang/Class;",
                         stat, DexLoadClass);
      AddIntrinsicMethod(dex_file, "getClassNameList", {"J"}, "[Ljava/lang/String;", stat,
                         DexGetClassNameList);
      AddIntrinsicMethod(dex_file, "closeDexFile", {"J"}, "Z", stat, DexCloseDexFile);
      AddIntrinsicMethod(dex_file, "openDexFile", {"Ljava/lang/String;"}, "J", stat,
                         DexOpenDexFile);
      AddIntrinsicMethod(dex_file, "loadDex",
                         {"Ljava/lang/String;", "Ljava/lang/String;", "I"}, "J", stat,
                         DexLoadDex);
    }

    for (VClass* k : {cl, bdcl, pcl, dcl, imcl, dex_file}) {
      if (k != nullptr) k->klass = class_class;
    }
  }

  // ---- M3 考纲第一批：jre-excluded 内建面 + 反射面 + hidden API（docs 14 章 28.4/29）----
  // 零壳适配：按 Android 语义建面，不识别壳、不特判、不伪造返回值。
  if (object != nullptr && class_class != nullptr) {
    const uint32_t ctor = kAccPublic | kAccConstructor;
    const uint32_t stat = kAccPublic | kAccStatic;
    const uint32_t pub = kAccPublic;
    constexpr uint32_t kAccIface = 0x200;
    constexpr uint32_t kAccAbstract = 0x400;
    auto reg = [&](VClass* k) {
      if (k != nullptr) k->klass = class_class;
      return k;
    };

    // P0 翻转：Iterable/Cloneable/AutoCloseable/Annotation/java.util.function 6 接口
    // 的内建注册删除——类定义由 runtime.dex 提供（java-lang-core/java-lang-annotation/
    // java-util-function 组件已放行；链接补全超类型改命中 lib 版）。

    // -- 异常类补全（IllegalAccessError/UnsatisfiedLinkError 等；docs 14 章 29）--
    VClass* cnfe = nullptr;  // 复用已建 ClassNotFoundException 链
    for (VClass* k : all) {
      if (k->descriptor == "Ljava/lang/ClassNotFoundException;") cnfe = k;
    }
    VClass* error_cls = nullptr;
    VClass* linkage = nullptr;
    VClass* roe = nullptr;
    VClass* ioe = nullptr;
    for (VClass* k : all) {
      if (k->descriptor == "Ljava/lang/Error;") error_cls = k;
      if (k->descriptor == "Ljava/lang/LinkageError;") linkage = k;
      if (k->descriptor == "Ljava/lang/ReflectiveOperationException;") roe = k;
      if (k->descriptor == "Ljava/io/IOException;") ioe = k;
    }
    struct {
      const char* desc;
      VClass* super;
    } kMoreEx[] = {
        // StackOverflowError 唯一注册点在 kExceptions（super=VirtualMachineError）：
        // 曾在此以 super=Error 二次注册，同名后建 VClass 在类表覆盖前者，导致
        // StackOverflowError.super 变成 Error（应为 VirtualMachineError）——已删。
        // 重复注册现为 AssertBuiltinRegistration 致命断言（双向校验口径）。
        {"Ljava/lang/AssertionError;", error_cls},
        {"Ljava/lang/IllegalAccessError;", linkage},
        {"Ljava/lang/UnsatisfiedLinkError;", linkage},
        {"Ljava/lang/IncompatibleClassChangeError;", linkage},
        {"Ljava/lang/ClassFormatError;", linkage},
        {"Ljava/lang/VerifyError;", linkage},
        {"Ljava/lang/SecurityException;", runtime_ex},
        {"Ljava/lang/IllegalAccessException;", roe},
        {"Ljava/lang/InstantiationException;", roe},
        {"Ljava/lang/NoSuchMethodException;", roe},
        {"Ljava/lang/NoSuchFieldException;", roe},
        {"Ljava/lang/InterruptedException;", exception},
        {"Ljava/lang/CloneNotSupportedException;", exception},
        {"Ljava/io/FileNotFoundException;", ioe},
    };
    for (const auto& e : kMoreEx) {
      if (e.super == nullptr) continue;
      reg(MakeClass(linker, &all, e.desc, e.super, 16, pub));
    }
    // AssertionError(Object)（OpenJDK/libcore 面）：assert 链上以 Throwable 实参
    // 构造（okhttp NamedRunnable 等），detailMessage=String.valueOf、实参为
    // Throwable 时 initCause。其余 (boolean)/(char)/... 变体按链上暴露再补。
    for (VClass* k : all) {
      if (k->descriptor == "Ljava/lang/AssertionError;") {
        AddIntrinsicMethod(k, "<init>", {"Ljava/lang/Object;"}, "V", ctor,
                           AssertionErrorInitObject);
      }
    }
    // InvocationTargetException：target 字段 @8（detailMessage @0 继承 Throwable）
    VClass* ite = reg(MakeClass(linker, &all, "Ljava/lang/reflect/InvocationTargetException;", roe,
                                24, pub));
    if (ite != nullptr) {
      VField tf;
      tf.declaring = ite;
      tf.name = "target";
      tf.type_descriptor = "Ljava/lang/Throwable;";
      tf.access_flags = pub;
      tf.offset = 16;  // cause@8（Throwable 布局）之上接 target
      tf.is_static = false;
      ite->instance_fields.push_back(std::move(tf));
    }

    // -- java.lang.reflect 全套（镜像布局契约 jni/reflection.cc：artX @0、
    //    declaringClass @8、accessible @16；docs 29：getDeclaredMethod/setAccessible）--
    VClass* member = reg(MakeClass(linker, &all, "Ljava/lang/reflect/Member;", object, 0,
                                   pub | kAccIface | kAccAbstract));
    if (member != nullptr) {
      AddMethodSkeleton(member, "getDeclaringClass", {}, "Ljava/lang/Class;", pub | kAccAbstract);
      AddMethodSkeleton(member, "getName", {}, "Ljava/lang/String;", pub | kAccAbstract);
      AddMethodSkeleton(member, "getModifiers", {}, "I", pub | kAccAbstract);
    }
    VClass* ao = reg(MakeClass(linker, &all, "Ljava/lang/reflect/AccessibleObject;", object, 0, pub));
    if (ao != nullptr) {
      AddIntrinsicMethod(ao, "setAccessible", {"Z"}, "V", pub, AOASetAccessible);
      AddIntrinsicMethod(ao, "isAccessible", {}, "Z", pub, AOAIsAccessible);
    }
    auto add_mirror_fields = [](VClass* k) {
      if (k == nullptr) return;
      VField art;
      art.declaring = k;
      art.name = k->descriptor.find("Field;") != std::string::npos ? "artField" : "artMethod";
      art.type_descriptor = "J";
      art.access_flags = pub;
      art.offset = 0;
      k->instance_fields.push_back(std::move(art));
      VField decl;
      decl.declaring = k;
      decl.name = "declaringClass";
      decl.type_descriptor = "Ljava/lang/Class;";
      decl.access_flags = pub;
      decl.offset = 8;
      k->instance_fields.push_back(std::move(decl));
      VField acc;
      acc.declaring = k;
      acc.name = "accessible";
      acc.type_descriptor = "Z";
      acc.access_flags = pub;
      acc.offset = 16;
      k->instance_fields.push_back(std::move(acc));
    };
    VClass* jl_method =
        reg(MakeClass(linker, &all, "Ljava/lang/reflect/Method;", ao, 24, pub | kAccFinal));
    VClass* jl_ctor = reg(
        MakeClass(linker, &all, "Ljava/lang/reflect/Constructor;", ao, 24, pub | kAccFinal));
    VClass* jl_field =
        reg(MakeClass(linker, &all, "Ljava/lang/reflect/Field;", ao, 24, pub | kAccFinal));
    add_mirror_fields(jl_method);
    add_mirror_fields(jl_ctor);
    add_mirror_fields(jl_field);
    for (VClass* k : {jl_method, jl_ctor, jl_field}) {
      if (k != nullptr && member != nullptr) k->interfaces = {member};
    }
    if (jl_method != nullptr) {
      AddIntrinsicMethod(jl_method, "getName", {}, "Ljava/lang/String;", pub, MemberGetName);
      AddIntrinsicMethod(jl_method, "getDeclaringClass", {}, "Ljava/lang/Class;", pub,
                         MemberGetDeclaringClass);
      AddIntrinsicMethod(jl_method, "getModifiers", {}, "I", pub, MemberGetModifiers);
      AddIntrinsicMethod(jl_method, "getParameterCount", {}, "I", pub, MethodGetParameterCount);
      AddIntrinsicMethod(jl_method, "isBridge", {}, "Z", pub, MethodIsBridge);
      AddIntrinsicMethod(jl_method, "isVarArgs", {}, "Z", pub, MethodIsVarArgs);
      AddIntrinsicMethod(jl_method, "isSynthetic", {}, "Z", pub, MethodIsSynthetic);
      AddIntrinsicMethod(jl_method, "isDefault", {}, "Z", pub, MethodIsDefault);
      AddIntrinsicMethod(jl_method, "getReturnType", {}, "Ljava/lang/Class;", pub,
                         MethodGetReturnType);
      AddIntrinsicMethod(jl_method, "getParameterTypes", {}, "[Ljava/lang/Class;", pub,
                         MethodGetParameterTypes);
      AddIntrinsicMethod(jl_method, "getParameterAnnotations", {},
                         "[[Ljava/lang/annotation/Annotation;", pub, MethodGetParameterAnnotations);
      AddIntrinsicMethod(jl_method, "getExceptionTypes", {}, "[Ljava/lang/Class;", pub,
                         MethodGetExceptionTypes);
      AddIntrinsicMethod(jl_method, "getGenericParameterTypes", {},
                         "[Ljava/lang/reflect/Type;", pub, MethodGetGenericParameterTypes);
      AddIntrinsicMethod(jl_method, "getGenericExceptionTypes", {},
                         "[Ljava/lang/reflect/Type;", pub, MethodGetGenericExceptionTypes);
      AddIntrinsicMethod(jl_method, "getGenericReturnType", {}, "Ljava/lang/reflect/Type;", pub,
                         MethodGetGenericReturnType);
      AddIntrinsicMethod(jl_method, "invoke",
                         {"Ljava/lang/Object;", "[Ljava/lang/Object;"},
                         "Ljava/lang/Object;", pub, MethodInvoke);
      // 注解族（getDeclaredAnnotations 族；dex 解码面见 builtins_reflect.cc）
      AddIntrinsicMethod(jl_method, "getAnnotation", {"Ljava/lang/Class;"},
                         "Ljava/lang/annotation/Annotation;", pub, MemberGetAnnotation);
      AddIntrinsicMethod(jl_method, "getDeclaredAnnotation", {"Ljava/lang/Class;"},
                         "Ljava/lang/annotation/Annotation;", pub, MemberGetAnnotation);
      AddIntrinsicMethod(jl_method, "getDeclaredAnnotations", {},
                         "[Ljava/lang/annotation/Annotation;", pub, MemberGetDeclaredAnnotations);
      AddIntrinsicMethod(jl_method, "getAnnotations", {},
                         "[Ljava/lang/annotation/Annotation;", pub, MemberGetDeclaredAnnotations);
      AddIntrinsicMethod(jl_method, "isAnnotationPresent", {"Ljava/lang/Class;"}, "Z", pub,
                         MemberIsAnnotationPresent);
    }
    if (jl_ctor != nullptr) {
      AddIntrinsicMethod(jl_ctor, "getName", {}, "Ljava/lang/String;", pub, MemberGetName);
      AddIntrinsicMethod(jl_ctor, "getDeclaringClass", {}, "Ljava/lang/Class;", pub,
                         MemberGetDeclaringClass);
      AddIntrinsicMethod(jl_ctor, "getModifiers", {}, "I", pub, MemberGetModifiers);
      AddIntrinsicMethod(jl_ctor, "getParameterTypes", {}, "[Ljava/lang/Class;", pub,
                         MethodGetParameterTypes);
      AddIntrinsicMethod(jl_ctor, "getParameterAnnotations", {},
                         "[[Ljava/lang/annotation/Annotation;", pub, MethodGetParameterAnnotations);
      AddIntrinsicMethod(jl_ctor, "getParameterCount", {}, "I", pub, MethodGetParameterCount);
      AddIntrinsicMethod(jl_ctor, "isVarArgs", {}, "Z", pub, MethodIsVarArgs);
      AddIntrinsicMethod(jl_ctor, "isSynthetic", {}, "Z", pub, MethodIsSynthetic);
      AddIntrinsicMethod(jl_ctor, "getGenericParameterTypes", {},
                         "[Ljava/lang/reflect/Type;", pub, MethodGetGenericParameterTypes);
      AddIntrinsicMethod(jl_ctor, "getGenericExceptionTypes", {},
                         "[Ljava/lang/reflect/Type;", pub, MethodGetGenericExceptionTypes);
      AddIntrinsicMethod(jl_ctor, "newInstance", {"[Ljava/lang/Object;"},
                         "Ljava/lang/Object;", pub, CtorNewInstance);
      AddIntrinsicMethod(jl_ctor, "getAnnotation", {"Ljava/lang/Class;"},
                         "Ljava/lang/annotation/Annotation;", pub, MemberGetAnnotation);
      AddIntrinsicMethod(jl_ctor, "getDeclaredAnnotation", {"Ljava/lang/Class;"},
                         "Ljava/lang/annotation/Annotation;", pub, MemberGetAnnotation);
      AddIntrinsicMethod(jl_ctor, "getDeclaredAnnotations", {},
                         "[Ljava/lang/annotation/Annotation;", pub, MemberGetDeclaredAnnotations);
      AddIntrinsicMethod(jl_ctor, "getAnnotations", {},
                         "[Ljava/lang/annotation/Annotation;", pub, MemberGetDeclaredAnnotations);
      AddIntrinsicMethod(jl_ctor, "isAnnotationPresent", {"Ljava/lang/Class;"}, "Z", pub,
                         MemberIsAnnotationPresent);
    }
    if (jl_field != nullptr) {
      AddIntrinsicMethod(jl_field, "getName", {}, "Ljava/lang/String;", pub, FieldGetName);
      AddIntrinsicMethod(jl_field, "getDeclaringClass", {}, "Ljava/lang/Class;", pub,
                         FieldGetDeclaringClass);
      AddIntrinsicMethod(jl_field, "getType", {}, "Ljava/lang/Class;", pub, FieldGetType);
      AddIntrinsicMethod(jl_field, "getGenericType", {}, "Ljava/lang/reflect/Type;", pub,
                         FieldGetGenericType);
      AddIntrinsicMethod(jl_field, "getModifiers", {}, "I", pub, FieldGetModifiers);
      AddIntrinsicMethod(jl_field, "isSynthetic", {}, "Z", pub, FieldIsSynthetic);
      AddIntrinsicMethod(jl_field, "isEnumConstant", {}, "Z", pub, FieldIsEnumConstant);
      AddIntrinsicMethod(jl_field, "getAnnotation", {"Ljava/lang/Class;"},
                         "Ljava/lang/annotation/Annotation;", pub, MemberGetAnnotation);
      AddIntrinsicMethod(jl_field, "getDeclaredAnnotation", {"Ljava/lang/Class;"},
                         "Ljava/lang/annotation/Annotation;", pub, MemberGetAnnotation);
      AddIntrinsicMethod(jl_field, "getDeclaredAnnotations", {},
                         "[Ljava/lang/annotation/Annotation;", pub, MemberGetDeclaredAnnotations);
      AddIntrinsicMethod(jl_field, "getAnnotations", {},
                         "[Ljava/lang/annotation/Annotation;", pub, MemberGetDeclaredAnnotations);
      AddIntrinsicMethod(jl_field, "isAnnotationPresent", {"Ljava/lang/Class;"}, "Z", pub,
                         MemberIsAnnotationPresent);
      AddIntrinsicMethod(jl_field, "get", {"Ljava/lang/Object;"}, "Ljava/lang/Object;", pub,
                         FieldGet);
      AddIntrinsicMethod(jl_field, "set", {"Ljava/lang/Object;", "Ljava/lang/Object;"}, "V", pub,
                         FieldSet);
      // typed 访问面（AOSP Field.get/setBoolean..get/setDouble 严格类型口径）
      AddIntrinsicMethod(jl_field, "getBoolean", {"Ljava/lang/Object;"}, "Z", pub,
                         FieldGetTyped<'Z'>);
      AddIntrinsicMethod(jl_field, "getByte", {"Ljava/lang/Object;"}, "B", pub,
                         FieldGetTyped<'B'>);
      AddIntrinsicMethod(jl_field, "getChar", {"Ljava/lang/Object;"}, "C", pub,
                         FieldGetTyped<'C'>);
      AddIntrinsicMethod(jl_field, "getShort", {"Ljava/lang/Object;"}, "S", pub,
                         FieldGetTyped<'S'>);
      AddIntrinsicMethod(jl_field, "getInt", {"Ljava/lang/Object;"}, "I", pub,
                         FieldGetTyped<'I'>);
      AddIntrinsicMethod(jl_field, "getLong", {"Ljava/lang/Object;"}, "J", pub,
                         FieldGetTyped<'J'>);
      AddIntrinsicMethod(jl_field, "getFloat", {"Ljava/lang/Object;"}, "F", pub,
                         FieldGetTyped<'F'>);
      AddIntrinsicMethod(jl_field, "getDouble", {"Ljava/lang/Object;"}, "D", pub,
                         FieldGetTyped<'D'>);
      AddIntrinsicMethod(jl_field, "setBoolean", {"Ljava/lang/Object;", "Z"}, "V", pub,
                         FieldSetTyped<'Z'>);
      AddIntrinsicMethod(jl_field, "setByte", {"Ljava/lang/Object;", "B"}, "V", pub,
                         FieldSetTyped<'B'>);
      AddIntrinsicMethod(jl_field, "setChar", {"Ljava/lang/Object;", "C"}, "V", pub,
                         FieldSetTyped<'C'>);
      AddIntrinsicMethod(jl_field, "setShort", {"Ljava/lang/Object;", "S"}, "V", pub,
                         FieldSetTyped<'S'>);
      AddIntrinsicMethod(jl_field, "setInt", {"Ljava/lang/Object;", "I"}, "V", pub,
                         FieldSetTyped<'I'>);
      AddIntrinsicMethod(jl_field, "setLong", {"Ljava/lang/Object;", "J"}, "V", pub,
                         FieldSetTyped<'J'>);
      AddIntrinsicMethod(jl_field, "setFloat", {"Ljava/lang/Object;", "F"}, "V", pub,
                         FieldSetTyped<'F'>);
      AddIntrinsicMethod(jl_field, "setDouble", {"Ljava/lang/Object;", "D"}, "V", pub,
                         FieldSetTyped<'D'>);
    }

    // -- java.lang.Class 反射面扩展（壳必用：getDeclaredMethod/Field 等）--
    if (class_class != nullptr) {
      AddIntrinsicMethod(class_class, "getDeclaredMethod",
                         {"Ljava/lang/String;", "[Ljava/lang/Class;"},
                         "Ljava/lang/reflect/Method;", pub, ClsGetDeclaredMethod);
      AddIntrinsicMethod(class_class, "getMethod",
                         {"Ljava/lang/String;", "[Ljava/lang/Class;"},
                         "Ljava/lang/reflect/Method;", pub, ClsGetMethod);
      AddIntrinsicMethod(class_class, "getDeclaredField", {"Ljava/lang/String;"},
                         "Ljava/lang/reflect/Field;", pub, ClsGetDeclaredField);
      AddIntrinsicMethod(class_class, "getField", {"Ljava/lang/String;"},
                         "Ljava/lang/reflect/Field;", pub, ClsGetField);
      AddIntrinsicMethod(class_class, "getDeclaredMethods", {}, "[Ljava/lang/reflect/Method;", pub,
                         ClsGetDeclaredMethods);
      AddIntrinsicMethod(class_class, "getMethods", {}, "[Ljava/lang/reflect/Method;", pub,
                         ClsGetMethods);
      AddIntrinsicMethod(class_class, "getDeclaringClass", {}, "Ljava/lang/Class;", pub,
                         ClsGetDeclaringClass);
      AddIntrinsicMethod(class_class, "getEnclosingClass", {}, "Ljava/lang/Class;", pub,
                         ClsGetEnclosingClass);
      AddIntrinsicMethod(class_class, "getEnclosingMethod", {},
                         "Ljava/lang/reflect/Method;", pub, ClsGetEnclosingMethod);
      AddIntrinsicMethod(class_class, "getDeclaredFields", {}, "[Ljava/lang/reflect/Field;", pub,
                         ClsGetDeclaredFields);
      AddIntrinsicMethod(class_class, "getFields", {}, "[Ljava/lang/reflect/Field;", pub,
                         ClsGetFields);
      AddIntrinsicMethod(class_class, "getConstructors", {},
                         "[Ljava/lang/reflect/Constructor;", pub, ClsGetConstructors);
      AddIntrinsicMethod(class_class, "getDeclaredConstructors", {},
                         "[Ljava/lang/reflect/Constructor;", pub, ClsGetDeclaredConstructors);
      AddIntrinsicMethod(class_class, "newInstance", {}, "Ljava/lang/Object;", pub, ClsNewInstance);
      AddIntrinsicMethod(class_class, "getSuperclass", {}, "Ljava/lang/Class;", pub,
                         ClsGetSuperclass);
      AddIntrinsicMethod(class_class, "getInterfaces", {}, "[Ljava/lang/Class;", pub,
                         ClsGetInterfaces);
      AddIntrinsicMethod(class_class, "isAssignableFrom", {"Ljava/lang/Class;"}, "Z", pub,
                         ClsIsAssignableFrom);
      AddIntrinsicMethod(class_class, "isInstance", {"Ljava/lang/Object;"}, "Z", pub,
                         ClsIsInstance);
      AddIntrinsicMethod(class_class, "cast", {"Ljava/lang/Object;"}, "Ljava/lang/Object;", pub,
                         ClsCast);
      AddIntrinsicMethod(class_class, "isInterface", {}, "Z", pub, ClsIsInterface);
      AddIntrinsicMethod(class_class, "isEnum", {}, "Z", pub, ClsIsEnum);
      AddIntrinsicMethod(class_class, "isAnnotation", {}, "Z", pub, ClsIsAnnotation);
      AddIntrinsicMethod(class_class, "isArray", {}, "Z", pub, ClsIsArray);
      AddIntrinsicMethod(class_class, "isPrimitive", {}, "Z", pub, ClsIsPrimitive);
      AddIntrinsicMethod(class_class, "isAnonymousClass", {}, "Z", pub, ClsIsAnonymousClass);
      AddIntrinsicMethod(class_class, "isLocalClass", {}, "Z", pub, ClsIsLocalClass);
      AddIntrinsicMethod(class_class, "isMemberClass", {}, "Z", pub, ClsIsMemberClass);
      AddIntrinsicMethod(class_class, "isSynthetic", {}, "Z", pub, ClsIsSynthetic);
      AddIntrinsicMethod(class_class, "getComponentType", {}, "Ljava/lang/Class;", pub,
                         ClsGetComponentType);
      AddIntrinsicMethod(class_class, "getSimpleName", {}, "Ljava/lang/String;", pub,
                         ClsGetSimpleName);
      AddIntrinsicMethod(class_class, "getCanonicalName", {}, "Ljava/lang/String;", pub,
                         ClsGetCanonicalName);
      AddIntrinsicMethod(class_class, "getTypeName", {}, "Ljava/lang/String;", pub, ClassGetName);
      AddIntrinsicMethod(class_class, "getModifiers", {}, "I", pub, ClsGetModifiers);
      AddIntrinsicMethod(class_class, "getEnumConstants", {}, "[Ljava/lang/Object;", pub,
                         ClsGetEnumConstants);
      // 包内借用面（jdk.internal.misc.JavaLangAccess.getEnumConstantsShared 的
      // 下游；VM 内建 Class 遮蔽 lib Class，需同名 intrinsic 承接）
      AddIntrinsicMethod(class_class, "getEnumConstantsShared", {}, "[Ljava/lang/Object;", pub,
                         ClsGetEnumConstants);
      AddIntrinsicMethod(class_class, "desiredAssertionStatus", {}, "Z", stat,
                         ClsDesiredAssertionStatus);
      AddIntrinsicMethod(class_class, "forName", {"Ljava/lang/String;"}, "Ljava/lang/Class;", stat,
                         ClsForName);
      AddIntrinsicMethod(class_class, "forName",
                         {"Ljava/lang/String;", "Z", "Ljava/lang/ClassLoader;"},
                         "Ljava/lang/Class;", stat, ClsForName);
      AddIntrinsicMethod(class_class, "getClassLoader", {}, "Ljava/lang/ClassLoader;", pub,
                         ClsGetClassLoader);
      AddIntrinsicMethod(class_class, "getPackage", {}, "Ljava/lang/Package;", pub,
                         ClsGetPackage);
      AddIntrinsicMethod(class_class, "getPackageName", {}, "Ljava/lang/String;", pub,
                         ClsGetPackageName);
      // P0 翻转接缝：getModule（unnamed module 单例，lib ServiceLoader checkCaller/
      // inExplicitModule/isNamed 链）+ getConstructor 系（反射面补齐，Constructor 镜像）
      AddIntrinsicMethod(class_class, "getModule", {}, "Ljava/lang/Module;", pub, ClsGetModule);
      AddIntrinsicMethod(class_class, "getConstructor", {"[Ljava/lang/Class;"},
                         "Ljava/lang/reflect/Constructor;", pub, ClsGetConstructor);
      AddIntrinsicMethod(class_class, "getDeclaredConstructor", {"[Ljava/lang/Class;"},
                         "Ljava/lang/reflect/Constructor;", pub, ClsGetDeclaredConstructor);
      // 泛型反射面（dex Signature 属性解析 → lib Type 镜像；M 级最小面）。
      // getGenericSuperclass/getGenericInterfaces/getTypeParameters;
      // 无签名退化 Class 形态（OpenJDK getGenericInfo()==null 分支语义）。
      AddIntrinsicMethod(class_class, "getGenericSuperclass", {}, "Ljava/lang/reflect/Type;", pub,
                         ClsGetGenericSuperclass);
      AddIntrinsicMethod(class_class, "getGenericInterfaces", {}, "[Ljava/lang/reflect/Type;", pub,
                         ClsGetGenericInterfaces);
      AddIntrinsicMethod(class_class, "getTypeParameters", {}, "[Ljava/lang/reflect/TypeVariable;",
                         pub, ClsGetTypeParameters);
      // AnnotatedElement.getDeclaredAnnotationsByType（Class 面补全:
      // getAnnotationsByType 同实现——Class 注解即声明注解）
      AddIntrinsicMethod(class_class, "getDeclaredAnnotationsByType", {"Ljava/lang/Class;"},
                         "[Ljava/lang/annotation/Annotation;", pub, ClassGetAnnotations);
      // Class implements Type/GenericDeclaration/AnnotatedElement（OpenJDK 形态;
      // 接口类由 runtime.dex 提供→延迟补挂;退化泛型语义返回 Class 形态时
      // checkcast/instanceof Type 需要此向量）。Serializable 后置。
      linker->AddPendingBuiltinInterface("Ljava/lang/Class;", "Ljava/lang/reflect/Type;");
      linker->AddPendingBuiltinInterface("Ljava/lang/Class;", "Ljava/lang/reflect/GenericDeclaration;");
      linker->AddPendingBuiltinInterface("Ljava/lang/Class;", "Ljava/lang/reflect/AnnotatedElement;");
    }

    // -- java.lang.System（常用静态面）--
    VClass* sys = reg(MakeClass(linker, &all, "Ljava/lang/System;", object, 0, pub | kAccFinal));
    if (sys != nullptr) {
      VField fin;
      fin.declaring = sys;
      fin.name = "in";
      fin.type_descriptor = "Ljava/io/InputStream;";
      fin.access_flags = stat;
      fin.offset = 0;
      fin.is_static = true;
      sys->static_fields.push_back(std::move(fin));
      VField fout;
      fout.declaring = sys;
      fout.name = "out";
      fout.type_descriptor = "Ljava/io/PrintStream;";
      fout.access_flags = stat;
      fout.offset = 1;
      fout.is_static = true;
      sys->static_fields.push_back(std::move(fout));
      VField ferr;
      ferr.declaring = sys;
      ferr.name = "err";
      ferr.type_descriptor = "Ljava/io/PrintStream;";
      ferr.access_flags = stat;
      ferr.offset = 2;
      ferr.is_static = true;
      sys->static_fields.push_back(std::move(ferr));
      sys->static_storage.assign(3, ZeroValue());
      AddIntrinsicMethod(sys, "arraycopy",
                         {"Ljava/lang/Object;", "I", "Ljava/lang/Object;", "I", "I"}, "V", stat,
                         SysArraycopy);
      AddIntrinsicMethod(sys, "currentTimeMillis", {}, "J", stat, SysCurrentTimeMillis);
      AddIntrinsicMethod(sys, "nanoTime", {}, "J", stat, SysNanoTime);
      AddIntrinsicMethod(sys, "identityHashCode", {"Ljava/lang/Object;"}, "I", stat,
                         SysIdentityHashCode);
      AddIntrinsicMethod(sys, "getProperty", {"Ljava/lang/String;"}, "Ljava/lang/String;", stat,
                         SysGetProperty);
      AddIntrinsicMethod(sys, "getProperty", {"Ljava/lang/String;", "Ljava/lang/String;"},
                         "Ljava/lang/String;", stat, SysGetProperty);
      AddIntrinsicMethod(sys, "getProperties", {}, "Ljava/util/Properties;", stat,
                         SysGetProperties);
      // 无 SecurityManager（lib ServiceLoader 等的 acc 探测面）
      AddIntrinsicMethod(sys, "getSecurityManager", {}, "Ljava/lang/SecurityManager;", stat,
                         SysGetSecurityManager);
      AddIntrinsicMethod(sys, "setProperty", {"Ljava/lang/String;", "Ljava/lang/String;"},
                         "Ljava/lang/String;", stat, SysSetProperty);
      AddIntrinsicMethod(sys, "clearProperty", {"Ljava/lang/String;"}, "Ljava/lang/String;", stat,
                         SysClearProperty);
      AddIntrinsicMethod(sys, "getenv", {"Ljava/lang/String;"}, "Ljava/lang/String;", stat,
                         SysGetenvName);
      AddIntrinsicMethod(sys, "lineSeparator", {}, "Ljava/lang/String;", stat, SysLineSeparator);
      AddIntrinsicMethod(sys, "mapLibraryName", {"Ljava/lang/String;"}, "Ljava/lang/String;", stat,
                         SysMapLibraryName);
      AddIntrinsicMethod(sys, "load", {"Ljava/lang/String;"}, "V", stat, SysLoad);
      AddIntrinsicMethod(sys, "loadLibrary", {"Ljava/lang/String;"}, "V", stat, SysLoadLibrary);
      AddIntrinsicMethod(sys, "gc", {}, "V", stat, SysGc);
    }

    // -- java.io.PrintStream 最小面（System.out/err）--
    VClass* ps = reg(MakeClass(linker, &all, "Ljava/io/PrintStream;", object, 8, pub));
    if (ps != nullptr) {
      VField sink;
      sink.declaring = ps;
      sink.name = "sink";
      sink.type_descriptor = "I";
      sink.access_flags = pub;
      sink.offset = 0;
      sink.is_static = false;
      ps->instance_fields.push_back(std::move(sink));
      AddIntrinsicMethod(ps, "println", {}, "V", pub, PSPrintlnNone);
      AddIntrinsicMethod(ps, "println", {"Ljava/lang/String;"}, "V", pub, PSPrintlnStr);
      AddIntrinsicMethod(ps, "println", {"Ljava/lang/Object;"}, "V", pub, PSPrintlnStr);
      AddIntrinsicMethod(ps, "println", {"I"}, "V", pub, PSPrintlnInt);
      AddIntrinsicMethod(ps, "println", {"J"}, "V", pub, PSPrintlnLong);
      AddIntrinsicMethod(ps, "println", {"Z"}, "V", pub, PSPrintlnBool);
      AddIntrinsicMethod(ps, "println", {"C"}, "V", pub, PSPrintlnChar);
      AddIntrinsicMethod(ps, "println", {"D"}, "V", pub, PSPrintlnDouble);
      AddIntrinsicMethod(ps, "println", {"F"}, "V", pub, PSPrintlnFloat);
      AddIntrinsicMethod(ps, "print", {"Ljava/lang/String;"}, "V", pub, PSPrintStr);
      AddIntrinsicMethod(ps, "print", {"Ljava/lang/Object;"}, "V", pub, PSPrintStr);
      AddIntrinsicMethod(ps, "flush", {}, "V", pub, SysGc);
      // System.out/err 语义面（<clinit> 等价填充——System 为内建类无字节码）：
      // 宿主控制台 PrintStream（PSWrite sink 1=stdout / 2=stderr）。
      if (sys != nullptr && sys->static_storage.size() >= 3) {
        VObject* out_ps = linker->AllocObject(ps);
        VObject* err_ps = linker->AllocObject(ps);
        VField* sink_f = FindInstField(ps, "sink");
        if (out_ps != nullptr && err_ps != nullptr && sink_f != nullptr) {
          SetIField(out_ps, sink_f->offset, 1);
          SetIField(err_ps, sink_f->offset, 2);
          sys->static_storage[1].ref = out_ps;
          sys->static_storage[2].ref = err_ps;
        }
      }
    }

    // -- java.lang.Runtime --
    VClass* rt = reg(MakeClass(linker, &all, "Ljava/lang/Runtime;", object, 0, pub));
    if (rt != nullptr) {
      VField cur;
      cur.declaring = rt;
      cur.name = "currentRuntime";
      cur.type_descriptor = "Ljava/lang/Runtime;";
      cur.access_flags = stat;
      cur.offset = 0;
      cur.is_static = true;
      rt->static_fields.push_back(std::move(cur));
      rt->static_storage.assign(1, ZeroValue());
      AddIntrinsicMethod(rt, "getRuntime", {}, "Ljava/lang/Runtime;", stat, RuntimeGetRuntime);
      AddIntrinsicMethod(rt, "loadLibrary", {"Ljava/lang/String;"}, "V", pub, RuntimeLoadLibrary);
      AddIntrinsicMethod(rt, "load", {"Ljava/lang/String;"}, "V", pub, RuntimeLoad);
      AddIntrinsicMethod(rt, "availableProcessors", {}, "I", pub, RuntimeAvailableProcessors);
      AddIntrinsicMethod(rt, "totalMemory", {}, "J", pub, RuntimeTotalMemory);
      AddIntrinsicMethod(rt, "maxMemory", {}, "J", pub, RuntimeMaxMemory);
      AddIntrinsicMethod(rt, "freeMemory", {}, "J", pub, RuntimeFreeMemory);
      AddIntrinsicMethod(rt, "gc", {}, "V", pub, RuntimeGc);
      // Runtime.exec：VM 无外部进程面 -> 哑进程（见 builtins_system.cc RuntimeExec）
      AddIntrinsicMethod(rt, "exec", {"Ljava/lang/String;"}, "Ljava/lang/Process;", pub,
                         RuntimeExec);
      AddIntrinsicMethod(rt, "exec", {"[Ljava/lang/String;"}, "Ljava/lang/Process;", pub,
                         RuntimeExec);
      AddIntrinsicMethod(rt, "exec", {"Ljava/lang/String;", "[Ljava/lang/String;"},
                         "Ljava/lang/Process;", pub, RuntimeExec);
      AddIntrinsicMethod(rt, "exec", {"[Ljava/lang/String;", "[Ljava/lang/String;"},
                         "Ljava/lang/Process;", pub, RuntimeExec);
      AddIntrinsicMethod(rt, "exec",
                         {"Ljava/lang/String;", "[Ljava/lang/String;", "Ljava/io/File;"},
                         "Ljava/lang/Process;", pub, RuntimeExec);
      AddIntrinsicMethod(rt, "exec",
                         {"[Ljava/lang/String;", "[Ljava/lang/String;", "Ljava/io/File;"},
                         "Ljava/lang/Process;", pub, RuntimeExec);
    }

    // -- java.lang.Enum（枚举子类超类；name @0、ordinal @8）--
    VClass* enm = reg(MakeClass(linker, &all, "Ljava/lang/Enum;", object, 16,
                                pub | kAccAbstract));
    if (enm != nullptr) {
      VField name_f;
      name_f.declaring = enm;
      name_f.name = "name";
      name_f.type_descriptor = "Ljava/lang/String;";
      name_f.access_flags = pub;
      name_f.offset = 0;
      name_f.is_static = false;
      enm->instance_fields.push_back(std::move(name_f));
      VField ord_f;
      ord_f.declaring = enm;
      ord_f.name = "ordinal";
      ord_f.type_descriptor = "I";
      ord_f.access_flags = pub;
      ord_f.offset = 8;
      ord_f.is_static = false;
      enm->instance_fields.push_back(std::move(ord_f));
      AddIntrinsicMethod(enm, "<init>", {"Ljava/lang/String;", "I"}, "V",
                         kAccProtected | kAccConstructor, EnumInit);
      AddIntrinsicMethod(enm, "name", {}, "Ljava/lang/String;", pub, EnumName);
      AddIntrinsicMethod(enm, "ordinal", {}, "I", pub, EnumOrdinal);
      AddIntrinsicMethod(enm, "toString", {}, "Ljava/lang/String;", pub, EnumToString);
      AddIntrinsicMethod(enm, "equals", {"Ljava/lang/Object;"}, "Z", pub, EnumEquals);
      AddIntrinsicMethod(enm, "hashCode", {}, "I", pub, EnumHashCode);
      AddIntrinsicMethod(enm, "compareTo", {"Ljava/lang/Enum;"}, "I", pub, EnumCompareToEnum);
      AddIntrinsicMethod(enm, "compareTo", {"Ljava/lang/Object;"}, "I", pub, EnumCompareToEnum);
      AddIntrinsicMethod(enm, "getDeclaringClass", {}, "Ljava/lang/Class;", pub,
                         EnumGetDeclaringClass);
      AddIntrinsicMethod(enm, "clone", {}, "Ljava/lang/Object;",
                         kAccProtected | kAccFinal, EnumClone);
      AddIntrinsicMethod(enm, "valueOf", {"Ljava/lang/Class;", "Ljava/lang/String;"},
                         "Ljava/lang/Enum;", stat, EnumValueOf);
    }

    // -- java.lang.Thread / ThreadGroup（jre-excluded）--
    VClass* tg = reg(MakeClass(linker, &all, "Ljava/lang/ThreadGroup;", object, 16, pub));
    if (tg != nullptr) {
      VField nf;
      nf.declaring = tg;
      nf.name = "name";
      nf.type_descriptor = "Ljava/lang/String;";
      nf.access_flags = pub;
      nf.offset = 0;
      nf.is_static = false;
      tg->instance_fields.push_back(std::move(nf));
      VField pf;
      pf.declaring = tg;
      pf.name = "parent";
      pf.type_descriptor = "Ljava/lang/ThreadGroup;";
      pf.access_flags = pub;
      pf.offset = 8;
      pf.is_static = false;
      tg->instance_fields.push_back(std::move(pf));
      AddIntrinsicMethod(tg, "<init>", {}, "V", ctor, ThreadGroupInit);
      AddIntrinsicMethod(tg, "<init>", {"Ljava/lang/String;"}, "V", ctor, ThreadGroupInit);
      AddIntrinsicMethod(tg, "<init>", {"Ljava/lang/ThreadGroup;", "Ljava/lang/String;"}, "V",
                         ctor, ThreadGroupInit);
      AddIntrinsicMethod(tg, "getName", {}, "Ljava/lang/String;", pub, ThreadGroupGetName);
      AddIntrinsicMethod(tg, "getParent", {}, "Ljava/lang/ThreadGroup;", pub, ThreadGroupGetParent);
      AddIntrinsicMethod(tg, "parentOf", {"Ljava/lang/ThreadGroup;"}, "Z", pub, ThreadGroupParentOf);
      AddIntrinsicMethod(tg, "activeCount", {}, "I", pub, ThreadGroupActiveCount);
      AddIntrinsicMethod(tg, "uncaughtException", {"Ljava/lang/Thread;", "Ljava/lang/Throwable;"},
                         "V", pub, ThreadGroupUncaughtException);
    }
    // -- java.lang.Thread$UncaughtExceptionHandler（UEH 骨架接口）--
    VClass* ueh_iface = reg(MakeClass(linker, &all, "Ljava/lang/Thread$UncaughtExceptionHandler;",
                                      object, 0, pub | kAccIface | kAccAbstract));
    if (ueh_iface != nullptr) {
      AddMethodSkeleton(ueh_iface, "uncaughtException",
                        {"Ljava/lang/Thread;", "Ljava/lang/Throwable;"}, "V", pub);
    }
    VClass* thr = reg(MakeClass(linker, &all, "Ljava/lang/Thread;", object, 104, pub));
    if (thr != nullptr) {
      struct {
        const char* name;
        const char* type;
        uint32_t off;
      } kThreadFields[] = {
          {"name", "Ljava/lang/String;", 0},
          {"priority", "I", 8},
          {"group", "Ljava/lang/ThreadGroup;", 16},
          {"target", "Ljava/lang/Runnable;", 24},
          {"tid", "J", 32},
          {"daemon", "I", 40},
          // ThreadLocal 链（runtime.dex java.lang.ThreadLocal get/set 语义面）
          {"threadLocals", "Ljava/lang/ThreadLocal$ThreadLocalMap;", 48},
          {"inheritableThreadLocals", "Ljava/lang/ThreadLocal$ThreadLocalMap;", 56},
          // M4.4：LockSupport 链（parkBlocker）+ ThreadLocalRandom 链
          //（objectFieldOffset 契约字段；ART thread.cc 同款槽位语义）
          {"parkBlocker", "Ljava/lang/Object;", 64},
          {"threadLocalRandomSecondarySeed", "I", 72},
          {"threadLocalRandomSeed", "J", 80},
          {"threadLocalRandomProbe", "I", 88},
          {"inheritedAccessControlContext", "Ljava/lang/Object;", 96},
      };
      for (const auto& tf : kThreadFields) {
        VField f;
        f.declaring = thr;
        f.name = tf.name;
        f.type_descriptor = tf.type;
        f.access_flags = pub;
        f.offset = tf.off;
        f.is_static = false;
        thr->instance_fields.push_back(std::move(f));
      }
      VField cur_t;
      cur_t.declaring = thr;
      cur_t.name = "theCurrentThread";
      cur_t.type_descriptor = "Ljava/lang/Thread;";
      cur_t.access_flags = stat;
      cur_t.offset = 0;
      cur_t.is_static = true;
      thr->static_fields.push_back(std::move(cur_t));
      // 线程上下文 ClassLoader（lib ServiceLoader.load(Class) 语义面；单线程 VM
      // 静态槽承载，gc.cc 扫描 static_storage 保活）
      VField ctx_l;
      ctx_l.declaring = thr;
      ctx_l.name = "contextClassLoader";
      ctx_l.type_descriptor = "Ljava/lang/ClassLoader;";
      ctx_l.access_flags = stat;
      ctx_l.offset = 1;
      ctx_l.is_static = true;
      thr->static_fields.push_back(std::move(ctx_l));
      // M4.4：allThreads 注册表（GC 根——挂起 GuestTask 的 Thread peer 保活）
      VField all_t;
      all_t.declaring = thr;
      all_t.name = "allThreads";
      all_t.type_descriptor = "[Ljava/lang/Thread;";
      all_t.access_flags = stat;
      all_t.offset = 2;
      all_t.is_static = true;
      thr->static_fields.push_back(std::move(all_t));
      thr->static_storage.assign(3, ZeroValue());
      // AOSP Thread <init> 8 形全集（ThreadPoolExecutor addWorker 等直指
      // (ThreadGroup,Runnable,String,J) 形；group-first 形参数按类型归位）
      AddIntrinsicMethod(thr, "<init>", {}, "V", ctor, ThreadInit);
      AddIntrinsicMethod(thr, "<init>", {"Ljava/lang/String;"}, "V", ctor, ThreadInit);
      AddIntrinsicMethod(thr, "<init>", {"Ljava/lang/Runnable;"}, "V", ctor, ThreadInit);
      AddIntrinsicMethod(thr, "<init>", {"Ljava/lang/Runnable;", "Ljava/lang/String;"}, "V", ctor,
                         ThreadInit);
      AddIntrinsicMethod(thr, "<init>", {"Ljava/lang/ThreadGroup;", "Ljava/lang/String;"}, "V",
                         ctor, ThreadInit);
      AddIntrinsicMethod(thr, "<init>", {"Ljava/lang/ThreadGroup;", "Ljava/lang/Runnable;"}, "V",
                         ctor, ThreadInit);
      AddIntrinsicMethod(thr, "<init>", {"Ljava/lang/ThreadGroup;", "Ljava/lang/Runnable;",
                                         "Ljava/lang/String;"},
                         "V", ctor, ThreadInit);
      AddIntrinsicMethod(thr, "<init>", {"Ljava/lang/ThreadGroup;", "Ljava/lang/Runnable;",
                                         "Ljava/lang/String;", "J"},
                         "V", ctor, ThreadInit);
      AddIntrinsicMethod(thr, "currentThread", {}, "Ljava/lang/Thread;", stat, ThreadCurrentThread);
      // 上下文 ClassLoader（lib ServiceLoader.load(Class) 等的调用面）
      AddIntrinsicMethod(thr, "getContextClassLoader", {}, "Ljava/lang/ClassLoader;", pub,
                         ThreadGetContextClassLoader);
      AddIntrinsicMethod(thr, "setContextClassLoader", {"Ljava/lang/ClassLoader;"}, "V", pub,
                         ThreadSetContextClassLoader);
      AddIntrinsicMethod(thr, "sleep", {"J"}, "V", stat, ThreadSleep);
      AddIntrinsicMethod(thr, "sleep", {"J", "I"}, "V", stat, ThreadSleep);
      AddIntrinsicMethod(thr, "yield", {}, "V", stat, ThreadYield);
      AddIntrinsicMethod(thr, "onSpinWait", {}, "V", stat, ThreadOnSpinWait);
      AddIntrinsicMethod(thr, "getName", {}, "Ljava/lang/String;", pub, ThreadGetName);
      AddIntrinsicMethod(thr, "setName", {"Ljava/lang/String;"}, "V", pub, ThreadSetName);
      AddIntrinsicMethod(thr, "getId", {}, "J", pub, ThreadGetId);
      AddIntrinsicMethod(thr, "getStackTrace", {}, "[Ljava/lang/StackTraceElement;", pub,
                         ThreadGetStackTrace);
      AddIntrinsicMethod(thr, "getAllStackTraces", {},
                         "Ljava/util/Map;", stat, ThreadGetAllStackTraces);
      AddIntrinsicMethod(thr, "getPriority", {}, "I", pub, ThreadGetPriority);
      AddIntrinsicMethod(thr, "setPriority", {"I"}, "V", pub, ThreadSetPriority);
      AddIntrinsicMethod(thr, "isAlive", {}, "Z", pub, ThreadIsAlive);
      AddIntrinsicMethod(thr, "isDaemon", {}, "Z", pub, ThreadIsDaemon);
      AddIntrinsicMethod(thr, "setDaemon", {"Z"}, "V", pub, ThreadSetDaemon);
      AddIntrinsicMethod(thr, "holdsLock", {"Ljava/lang/Object;"}, "Z", stat, ThreadHoldsLock);
      AddIntrinsicMethod(thr, "getThreadGroup", {}, "Ljava/lang/ThreadGroup;", pub,
                         ThreadGetGroup);
      // M4.4 线程语义面（GuestTask 1:1；docs 04 章 14.2）：start/join/interrupt
      AddIntrinsicMethod(thr, "start", {}, "V", pub, ThreadStart);
      AddIntrinsicMethod(thr, "run", {}, "V", pub, ThreadRun);
      AddIntrinsicMethod(thr, "join", {}, "V", pub | kAccFinal, ThreadJoin);
      AddIntrinsicMethod(thr, "join", {"J"}, "V", pub | kAccFinal, ThreadJoin);
      AddIntrinsicMethod(thr, "join", {"J", "I"}, "V", pub | kAccFinal, ThreadJoin);
      AddIntrinsicMethod(thr, "interrupt", {}, "V", pub, ThreadInterrupt);
      AddIntrinsicMethod(thr, "isInterrupted", {}, "Z", pub, ThreadIsInterrupted);
      AddIntrinsicMethod(thr, "interrupted", {}, "Z", stat, ThreadInterrupted);
      // UEH 最小骨架（Java 面 start/join/UEH 目标态归 lib，本轮随内建面先行）
      AddIntrinsicMethod(thr, "getUncaughtExceptionHandler", {},
                         "Ljava/lang/Thread$UncaughtExceptionHandler;", pub, ThreadGetUEH);
      AddIntrinsicMethod(thr, "setUncaughtExceptionHandler",
                         {"Ljava/lang/Thread$UncaughtExceptionHandler;"}, "V", pub, ThreadSetUEH);
      AddIntrinsicMethod(thr, "setDefaultUncaughtExceptionHandler",
                         {"Ljava/lang/Thread$UncaughtExceptionHandler;"}, "V", stat,
                         ThreadSetDefaultUEH);
      AddIntrinsicMethod(thr, "getDefaultUncaughtExceptionHandler", {},
                         "Ljava/lang/Thread$UncaughtExceptionHandler;", stat,
                         ThreadGetDefaultUEH);
      AddIntrinsicMethod(thr, "getState", {}, "Ljava/lang/Thread$State;", pub, ThreadGetState);
    }

    // -- java.lang.Thread$State（Thread.getState 枚举面；Enum 子类）--
    // 常量实例静态槽预置（NEW=0 … TERMINATED=5；名/序同 javac 枚举产物），
    // GC 经静态根扫描保活（gc.cc 扫 static_storage）。
    if (enm != nullptr) {
      VClass* tstate = reg(
          MakeClass(linker, &all, "Ljava/lang/Thread$State;", enm, 16, pub | kAccFinal));
      if (tstate != nullptr) {
        static const char* kStateNames[] = {"NEW",      "RUNNABLE",       "BLOCKED",
                                            "WAITING",  "TIMED_WAITING",  "TERMINATED"};
        tstate->static_storage.assign(6, ZeroValue());
        for (int i = 0; i < 6; i++) {
          VField f;
          f.declaring = tstate;
          f.name = kStateNames[i];
          f.type_descriptor = "Ljava/lang/Thread$State;";
          f.access_flags = pub | stat | kAccFinal | 0x4000u;  // ACC_ENUM
          f.offset = static_cast<uint32_t>(i);
          f.is_static = true;
          tstate->static_fields.push_back(std::move(f));
          VObject* inst = linker->AllocObject(tstate);
          if (inst == nullptr) continue;
          std::u16string nm;
          for (const char* c = kStateNames[i]; *c != '\0'; ++c) {
            nm.push_back(static_cast<char16_t>(*c));
          }
          VString* ns = linker->AllocString(nm);
          if (ns != nullptr) {
            ObjHeader** slot = reinterpret_cast<ObjHeader**>(inst->field_data() + 0);
            *slot = ns;  // Enum.name @0
          }
          // Enum.ordinal @8：8 字节槽全宽写（EnumInit 同口径，防半槽脏高 32 位）
          uint64_t ord = static_cast<uint64_t>(static_cast<uint32_t>(i));
          std::memcpy(inst->field_data() + 8, &ord, sizeof(ord));
          tstate->static_storage[i].ref = inst;
        }
      }
    }

    // -- dalvik.system.VMRuntime hidden API 面（docs 29.2③：不设真实限制）--
    VClass* vmr = reg(MakeClass(linker, &all, "Ldalvik/system/VMRuntime;", object, 16, pub));
    if (vmr != nullptr) {
      VField ex;
      ex.declaring = vmr;
      ex.name = "hiddenApiExemptions";
      ex.type_descriptor = "[Ljava/lang/String;";
      ex.access_flags = pub;
      ex.offset = 0;
      ex.is_static = false;
      vmr->instance_fields.push_back(std::move(ex));
      VField util;
      util.declaring = vmr;
      util.name = "targetHeapUtilization";
      util.type_descriptor = "D";
      util.access_flags = pub;
      util.offset = 8;
      util.is_static = false;
      vmr->instance_fields.push_back(std::move(util));
      VField cur_r;
      cur_r.declaring = vmr;
      cur_r.name = "currentRuntime";
      cur_r.type_descriptor = "Ldalvik/system/VMRuntime;";
      cur_r.access_flags = stat;
      cur_r.offset = 0;
      cur_r.is_static = true;
      vmr->static_fields.push_back(std::move(cur_r));
      vmr->static_storage.assign(1, ZeroValue());
      AddIntrinsicMethod(vmr, "getRuntime", {}, "Ldalvik/system/VMRuntime;", stat,
                         VMRuntimeGetRuntime);
      AddIntrinsicMethod(vmr, "setHiddenApiExemptions", {"[Ljava/lang/String;"}, "V", pub,
                         VMRuntimeSetHiddenApiExemptions);
      AddIntrinsicMethod(vmr, "setHiddenApiAccessLogSampling", {"I"}, "V", pub,
                         VMRuntimeSetHiddenApiAccessLogSampling);
      AddIntrinsicMethod(vmr, "getTargetHeapUtilization", {}, "D", pub,
                         VMRuntimeGetTargetHeapUtilization);
      AddIntrinsicMethod(vmr, "setTargetHeapUtilization", {"D"}, "V", pub,
                         VMRuntimeSetTargetHeapUtilization);
    }

    // -- M4.2 考卷链：jdk.internal.misc 面（jre-excluded VM 内建职责）--
    // Unsafe：AtomicInteger（runtime.dex）<clinit>/getAndAddInt 的原子槽语义。
    VClass* unsafe_cls =
        reg(MakeClass(linker, &all, "Ljdk/internal/misc/Unsafe;", object, 0, pub));
    if (unsafe_cls != nullptr) {
      VField the_unsafe;
      the_unsafe.declaring = unsafe_cls;
      the_unsafe.name = "theUnsafe";
      the_unsafe.type_descriptor = "Ljdk/internal/misc/Unsafe;";
      the_unsafe.access_flags = stat;
      the_unsafe.offset = 0;
      the_unsafe.is_static = true;
      unsafe_cls->static_fields.push_back(std::move(the_unsafe));
      unsafe_cls->static_storage.assign(1, ZeroValue());
      AddIntrinsicMethod(unsafe_cls, "getUnsafe", {}, "Ljdk/internal/misc/Unsafe;", stat,
                         UnsafeGetUnsafe);
      AddIntrinsicMethod(unsafe_cls, "isBigEndian", {}, "Z", pub, UnsafeIsBigEndian);
      // M4.4 park/unpark（LockSupport/AQS 链；协程 wait/wake 承接）
      AddIntrinsicMethod(unsafe_cls, "park", {"Z", "J"}, "V", pub, UnsafePark);
      AddIntrinsicMethod(unsafe_cls, "unpark", {"Ljava/lang/Thread;"}, "V", pub, UnsafeUnpark);
      AddIntrinsicMethod(unsafe_cls, "objectFieldOffset", {"Ljava/lang/Class;", "Ljava/lang/String;"},
                         "J", pub, UnsafeObjectFieldOffset);
      AddIntrinsicMethod(unsafe_cls, "objectFieldOffset", {"Ljava/lang/reflect/Field;"}, "J", pub,
                         UnsafeObjectFieldOffsetF);
      AddIntrinsicMethod(unsafe_cls, "getInt", {"Ljava/lang/Object;", "J"}, "I", pub, UnsafeGetInt);
      AddIntrinsicMethod(unsafe_cls, "putInt", {"Ljava/lang/Object;", "J", "I"}, "V", pub,
                         UnsafePutInt);
      AddIntrinsicMethod(unsafe_cls, "putIntRelease", {"Ljava/lang/Object;", "J", "I"}, "V", pub,
                         UnsafePutInt);
      AddIntrinsicMethod(unsafe_cls, "putIntOpaque", {"Ljava/lang/Object;", "J", "I"}, "V", pub,
                         UnsafePutInt);
      AddIntrinsicMethod(unsafe_cls, "getIntAcquire", {"Ljava/lang/Object;", "J"}, "I", pub,
                         UnsafeGetInt);
      AddIntrinsicMethod(unsafe_cls, "getIntOpaque", {"Ljava/lang/Object;", "J"}, "I", pub,
                         UnsafeGetInt);
      AddIntrinsicMethod(unsafe_cls, "getAndAddInt", {"Ljava/lang/Object;", "J", "I"}, "I", pub,
                         UnsafeGetAndAddInt);
      AddIntrinsicMethod(unsafe_cls, "getAndSetInt", {"Ljava/lang/Object;", "J", "I"}, "I", pub,
                         UnsafeGetAndSetInt);
      AddIntrinsicMethod(unsafe_cls, "compareAndSetInt",
                         {"Ljava/lang/Object;", "J", "I", "I"}, "Z", pub, UnsafeCompareAndSetInt);
      for (const char* cas_name :
           {"weakCompareAndSetIntPlain", "weakCompareAndSetInt", "weakCompareAndSetIntAcquire",
            "weakCompareAndSetIntRelease"}) {
        AddIntrinsicMethod(unsafe_cls, cas_name, {"Ljava/lang/Object;", "J", "I", "I"}, "Z", pub,
                           UnsafeCompareAndSetInt);
      }
      for (const char* cae_name :
           {"compareAndExchangeInt", "compareAndExchangeIntAcquire", "compareAndExchangeIntRelease"}) {
        AddIntrinsicMethod(unsafe_cls, cae_name, {"Ljava/lang/Object;", "J", "I", "I"}, "I", pub,
                           UnsafeCompareAndExchangeInt);
      }
      // long 槽面（java.util.concurrent.atomic.AtomicLong lib 本体 CAS 承接）
      for (const char* get_name : {"getLong", "getLongVolatile", "getLongAcquire", "getLongOpaque"}) {
        AddIntrinsicMethod(unsafe_cls, get_name, {"Ljava/lang/Object;", "J"}, "J", pub,
                           UnsafeGetLong);
      }
      for (const char* put_name : {"putLong", "putLongVolatile", "putLongRelease", "putLongOpaque"}) {
        AddIntrinsicMethod(unsafe_cls, put_name, {"Ljava/lang/Object;", "J", "J"}, "V", pub,
                           UnsafePutLong);
      }
      AddIntrinsicMethod(unsafe_cls, "getAndSetLong", {"Ljava/lang/Object;", "J", "J"}, "J", pub,
                         UnsafeGetAndSetLong);
      AddIntrinsicMethod(unsafe_cls, "getAndAddLong", {"Ljava/lang/Object;", "J", "J"}, "J", pub,
                         UnsafeGetAndAddLong);
      AddIntrinsicMethod(unsafe_cls, "compareAndSetLong",
                         {"Ljava/lang/Object;", "J", "J", "J"}, "Z", pub, UnsafeCompareAndSetLong);
      for (const char* wcas_l : {"weakCompareAndSetLongPlain", "weakCompareAndSetLong",
                                 "weakCompareAndSetLongAcquire", "weakCompareAndSetLongRelease"}) {
        AddIntrinsicMethod(unsafe_cls, wcas_l, {"Ljava/lang/Object;", "J", "J", "J"}, "Z", pub,
                           UnsafeCompareAndSetLong);
      }
      for (const char* cae_l : {"compareAndExchangeLong", "compareAndExchangeLongAcquire",
                                "compareAndExchangeLongRelease"}) {
        AddIntrinsicMethod(unsafe_cls, cae_l, {"Ljava/lang/Object;", "J", "J", "J"}, "J", pub,
                           UnsafeCompareAndExchangeLong);
      }
      // int/long volatile/ordered 镜像（putObjectVolatile/getObjectVolatile/
      // putOrderedObject 同型族；JDK9+ Acquire/Release/Opaque 别名一并成批）。
      for (const char* get_iv : {"getIntVolatile"}) {
        AddIntrinsicMethod(unsafe_cls, get_iv, {"Ljava/lang/Object;", "J"}, "I", pub,
                           UnsafeGetInt);
      }
      for (const char* put_iv : {"putIntVolatile", "putOrderedInt"}) {
        AddIntrinsicMethod(unsafe_cls, put_iv, {"Ljava/lang/Object;", "J", "I"}, "V", pub,
                           UnsafePutInt);
      }
      AddIntrinsicMethod(unsafe_cls, "putOrderedLong", {"Ljava/lang/Object;", "J", "J"}, "V", pub,
                         UnsafePutLong);
      // 对象槽面（putObjectVolatile/getObjectVolatile/compareAndSetObject/
      // getAndSetObject/putOrderedObject 族 + Reference 命名镜像）。
      for (const char* get_o : {"getObject", "getObjectVolatile", "getObjectAcquire",
                                "getObjectOpaque", "getReference", "getReferenceVolatile",
                                "getReferenceAcquire", "getReferenceOpaque"}) {
        AddIntrinsicMethod(unsafe_cls, get_o, {"Ljava/lang/Object;", "J"}, "Ljava/lang/Object;",
                           pub, UnsafeGetObject);
      }
      for (const char* put_o : {"putObject", "putObjectVolatile", "putObjectRelease",
                                "putObjectOpaque", "putOrderedObject", "putReference",
                                "putReferenceVolatile", "putReferenceRelease",
                                "putReferenceOpaque", "putOrderedReference"}) {
        AddIntrinsicMethod(unsafe_cls, put_o,
                           {"Ljava/lang/Object;", "J", "Ljava/lang/Object;"}, "V", pub,
                           UnsafePutObject);
      }
      for (const char* gas_o : {"getAndSetObject", "getAndSetReference"}) {
        AddIntrinsicMethod(unsafe_cls, gas_o,
                           {"Ljava/lang/Object;", "J", "Ljava/lang/Object;"},
                           "Ljava/lang/Object;", pub, UnsafeGetAndSetObject);
      }
      for (const char* cas_o : {"compareAndSetObject", "weakCompareAndSetObject",
                                "weakCompareAndSetObjectPlain", "weakCompareAndSetObjectAcquire",
                                "weakCompareAndSetObjectRelease", "compareAndSetReference",
                                "weakCompareAndSetReference", "weakCompareAndSetReferencePlain",
                                "weakCompareAndSetReferenceAcquire",
                                "weakCompareAndSetReferenceRelease"}) {
        AddIntrinsicMethod(unsafe_cls, cas_o,
                           {"Ljava/lang/Object;", "J", "Ljava/lang/Object;",
                            "Ljava/lang/Object;"},
                           "Z", pub, UnsafeCompareAndSetObject);
      }
      for (const char* cae_o : {"compareAndExchangeObject", "compareAndExchangeObjectAcquire",
                                "compareAndExchangeObjectRelease", "compareAndExchangeReference",
                                "compareAndExchangeReferenceAcquire",
                                "compareAndExchangeReferenceRelease"}) {
        AddIntrinsicMethod(unsafe_cls, cae_o,
                           {"Ljava/lang/Object;", "J", "Ljava/lang/Object;",
                            "Ljava/lang/Object;"},
                           "Ljava/lang/Object;", pub, UnsafeCompareAndExchangeObject);
      }
      // 内存屏障 + 数组基址/步长（ConcurrentHashMap <clinit> 等 juc 面）
      for (const char* fence : {"storeFence", "loadFence", "fullFence", "storeStoreFence"}) {
        AddIntrinsicMethod(unsafe_cls, fence, {}, "V", pub, UnsafeFence);
      }
      AddIntrinsicMethod(unsafe_cls, "arrayBaseOffset", {"Ljava/lang/Class;"}, "I", pub,
                         UnsafeArrayBaseOffset);
      AddIntrinsicMethod(unsafe_cls, "arrayIndexScale", {"Ljava/lang/Class;"}, "I", pub,
                         UnsafeArrayIndexScale);
      AddIntrinsicMethod(unsafe_cls, "addressSize", {}, "I", pub, UnsafeArrayIndexScale);
      // -- sun.misc.Unsafe legacy 门面转发目标（lib 壳 → 本面；§31.7 A 档原语缝）--
      AddIntrinsicMethod(unsafe_cls, "allocateInstance", {"Ljava/lang/Class;"},
                         "Ljava/lang/Object;", pub, UnsafeAllocateInstance);
      AddIntrinsicMethod(unsafe_cls, "staticFieldOffset", {"Ljava/lang/reflect/Field;"}, "J", pub,
                         UnsafeStaticFieldOffset);
      AddIntrinsicMethod(unsafe_cls, "staticFieldBase", {"Ljava/lang/reflect/Field;"},
                         "Ljava/lang/Object;", pub, UnsafeStaticFieldBase);
      AddIntrinsicMethod(unsafe_cls, "shouldBeInitialized", {"Ljava/lang/Class;"}, "Z", pub,
                         UnsafeShouldBeInitialized);
      AddIntrinsicMethod(unsafe_cls, "ensureClassInitialized", {"Ljava/lang/Class;"}, "V", pub,
                         UnsafeEnsureClassInitialized);
      AddIntrinsicMethod(unsafe_cls, "throwException", {"Ljava/lang/Throwable;"}, "V", pub,
                         UnsafeThrowException);
      // typed 槽面（Z/B/S/C/F/D）+ Volatile 别名（单线程 VM 与 plain 同型）
      struct TypedSlot {
        const char* get;
        const char* put;
        const char* type;
        IntrinsicFn get_fn;
        IntrinsicFn put_fn;
      };
      static const TypedSlot kTypedSlots[] = {
          {"getBoolean", "putBoolean", "Z", UnsafeGetBoolean, UnsafePutBoolean},
          {"getByte", "putByte", "B", UnsafeGetByte, UnsafePutByte},
          {"getShort", "putShort", "S", UnsafeGetShort, UnsafePutShort},
          {"getChar", "putChar", "C", UnsafeGetChar, UnsafePutChar},
          {"getFloat", "putFloat", "F", UnsafeGetFloat, UnsafePutFloat},
          {"getDouble", "putDouble", "D", UnsafeGetDouble, UnsafePutDouble},
      };
      for (const TypedSlot& t : kTypedSlots) {
        std::string get_v = std::string(t.get) + "Volatile";
        std::string put_v = std::string(t.put) + "Volatile";
        AddIntrinsicMethod(unsafe_cls, t.get, {"Ljava/lang/Object;", "J"}, t.type, pub, t.get_fn);
        AddIntrinsicMethod(unsafe_cls, t.put, {"Ljava/lang/Object;", "J", t.type}, "V", pub,
                           t.put_fn);
        AddIntrinsicMethod(unsafe_cls, get_v.c_str(), {"Ljava/lang/Object;", "J"}, t.type, pub,
                           t.get_fn);
        AddIntrinsicMethod(unsafe_cls, put_v.c_str(), {"Ljava/lang/Object;", "J", t.type}, "V",
                           pub, t.put_fn);
      }
      // 地址形字节面（getByte(long)/putByte(long,byte)：绝对地址单字节——
      // java.nio Direct/MappedByteBuffer mmap 数据面底层缝）
      AddIntrinsicMethod(unsafe_cls, "getByte", {"J"}, "B", pub, UnsafeGetByteAddress);
      AddIntrinsicMethod(unsafe_cls, "putByte", {"J", "B"}, "V", pub, UnsafePutByteAddress);
      // 原生内存面（sun.misc.Unsafe address 形转发目标；Netty off-heap /
      // DirectByteBuffer 消费）：typed 直读直写 + allocate/free/copy/set。
      AddIntrinsicMethod(unsafe_cls, "getShort", {"J"}, "S", pub, UnsafeGetShortAddress);
      AddIntrinsicMethod(unsafe_cls, "putShort", {"J", "S"}, "V", pub, UnsafePutShortAddress);
      AddIntrinsicMethod(unsafe_cls, "getChar", {"J"}, "C", pub, UnsafeGetCharAddress);
      AddIntrinsicMethod(unsafe_cls, "putChar", {"J", "C"}, "V", pub, UnsafePutCharAddress);
      AddIntrinsicMethod(unsafe_cls, "getInt", {"J"}, "I", pub, UnsafeGetIntAddress);
      AddIntrinsicMethod(unsafe_cls, "putInt", {"J", "I"}, "V", pub, UnsafePutIntAddress);
      AddIntrinsicMethod(unsafe_cls, "getLong", {"J"}, "J", pub, UnsafeGetLongAddress);
      AddIntrinsicMethod(unsafe_cls, "putLong", {"J", "J"}, "V", pub, UnsafePutLongAddress);
      AddIntrinsicMethod(unsafe_cls, "getFloat", {"J"}, "F", pub, UnsafeGetFloatAddress);
      AddIntrinsicMethod(unsafe_cls, "putFloat", {"J", "F"}, "V", pub, UnsafePutFloatAddress);
      AddIntrinsicMethod(unsafe_cls, "getDouble", {"J"}, "D", pub, UnsafeGetDoubleAddress);
      AddIntrinsicMethod(unsafe_cls, "putDouble", {"J", "D"}, "V", pub, UnsafePutDoubleAddress);
      AddIntrinsicMethod(unsafe_cls, "allocateMemory", {"J"}, "J", pub, UnsafeAllocateMemory);
      AddIntrinsicMethod(unsafe_cls, "reallocateMemory", {"J", "J"}, "J", pub,
                         UnsafeReallocateMemory);
      AddIntrinsicMethod(unsafe_cls, "freeMemory", {"J"}, "V", pub, UnsafeFreeMemory);
      AddIntrinsicMethod(unsafe_cls, "setMemory", {"J", "J", "B"}, "V", pub,
                         UnsafeSetMemoryAddress);
      AddIntrinsicMethod(unsafe_cls, "setMemory",
                         {"Ljava/lang/Object;", "J", "J", "B"}, "V", pub,
                         UnsafeSetMemoryObject);
      AddIntrinsicMethod(unsafe_cls, "copyMemory", {"J", "J", "J"}, "V", pub,
                         UnsafeCopyMemoryAddress);
      AddIntrinsicMethod(unsafe_cls, "copyMemory",
                         {"Ljava/lang/Object;", "J", "Ljava/lang/Object;", "J", "J"}, "V", pub,
                         UnsafeCopyMemoryObject);
    }
    // P0 翻转：AtomicBoolean/AtomicLong/AtomicReference/AtomicReferenceArray 与
    // jdk.internal.misc.TerminatingThreadLocal 的内建注册删除（§31.3 第 5 步）。
    // 原子类 lib 版已入包：CAS 需求由 native 面承接——AtomicLong 走 Unsafe long
    // 槽（Unsafe 面扩展），VarHandle 形态三类经 BindWellKnownNatives 将 CAS 方法
    // 绑 VM intrinsic（java.lang.invoke 后置）；TerminatingThreadLocal 改 lib 版
    // （extends ThreadLocal，对齐 OpenJDK）。
    // -- M4.2 考卷链：java.lang.ref 面（ThreadLocalMap.Entry extends WeakReference）--
    // 单线程 VM 无 Reference Handler 线程（OpenJDK <clinit> 不落）：referent 强语义。
    VClass* ref_queue =
        reg(MakeClass(linker, &all, "Ljava/lang/ref/ReferenceQueue;", object, 8, pub));
    if (ref_queue != nullptr) {
      VField head;
      head.declaring = ref_queue;
      head.name = "head";
      head.type_descriptor = "Ljava/lang/ref/Reference;";
      head.access_flags = pub;
      head.offset = 0;
      head.is_static = false;
      ref_queue->instance_fields.push_back(std::move(head));
      VField null_q;
      null_q.declaring = ref_queue;
      null_q.name = "NULL";
      null_q.type_descriptor = "Ljava/lang/ref/ReferenceQueue;";
      null_q.access_flags = stat;
      null_q.offset = 0;
      null_q.is_static = true;
      ref_queue->static_fields.push_back(std::move(null_q));
      ref_queue->static_storage.assign(1, ZeroValue());
      VObject* null_inst = linker->AllocObject(ref_queue);
      if (null_inst != nullptr) ref_queue->static_storage[0].ref = null_inst;
      AddIntrinsicMethod(ref_queue, "enqueue", {"Ljava/lang/ref/Reference;"}, "Z", pub, RefFalse);
      AddIntrinsicMethod(ref_queue, "poll", {}, "Ljava/lang/ref/Reference;", pub, RefQueuePoll);
    }
    VClass* reference =
        reg(MakeClass(linker, &all, "Ljava/lang/ref/Reference;", object, 32, pub));
    if (reference != nullptr) {
      struct {
        const char* name;
        const char* type;
        uint32_t off;
      } kRefFields[] = {
          {"referent", "Ljava/lang/Object;", 0},
          {"queue", "Ljava/lang/ref/ReferenceQueue;", 8},
          {"next", "Ljava/lang/ref/Reference;", 16},
          {"discovered", "Ljava/lang/ref/Reference;", 24},
      };
      for (const auto& rf : kRefFields) {
        VField f;
        f.declaring = reference;
        f.name = rf.name;
        f.type_descriptor = rf.type;
        f.access_flags = pub;
        f.offset = rf.off;
        f.is_static = false;
        reference->instance_fields.push_back(std::move(f));
      }
      AddIntrinsicMethod(reference, "<init>", {"Ljava/lang/Object;"}, "V", ctor, RefInit1);
      AddIntrinsicMethod(reference, "<init>", {"Ljava/lang/Object;", "Ljava/lang/ref/ReferenceQueue;"},
                         "V", ctor, RefInit2);
      AddIntrinsicMethod(reference, "get", {}, "Ljava/lang/Object;", pub, RefGet);
      AddIntrinsicMethod(reference, "clear", {}, "V", pub, RefClear);
      AddIntrinsicMethod(reference, "isEnqueued", {}, "Z", pub, RefFalse);
      AddIntrinsicMethod(reference, "enqueue", {}, "Z", pub, RefFalse);
      AddIntrinsicMethod(reference, "reachabilityFence", {"Ljava/lang/Object;"}, "V", stat,
                         RefNoop);
    }
    VClass* weak_ref =
        reg(MakeClass(linker, &all, "Ljava/lang/ref/WeakReference;", reference, 32, pub));
    if (weak_ref != nullptr) {
      // <init>()V 无码 = 空实现（referent 保持 null）；(Object)/(Object,ReferenceQueue)
      // 沿 super 链解析到 Reference.<init> intrinsic。
      AddMethodSkeleton(weak_ref, "<init>", {}, "V", ctor);
    }
  }

  // ==== M5：ClassLoader.getResource(s) 资源面（URL/Enumeration 由 runtime.dex 提供）====
  // P0 翻转：java.util.ServiceLoader/$LazyIterator/ServiceConfigurationError/
  // NoSuchElementException、java.net.URL、sun.misc.CompoundEnumeration 的内建注册
  // 删除（§31.3 第 5 步）——类定义由 runtime.dex 提供（lib ServiceLoader 为 JDK9
  // 形态：LazyClassPathLookupIterator/ModuleServicesLookupIterator 内部类）。
  // getResources 接缝改接 lib URL 字段布局与 lib Enumeration（见 MakeURL/
  // CLGetResources）；ServiceLoader 的资源读取面绑 native（parse，见
  // BindWellKnownNatives 覆写表）。
  if (object != nullptr && class_class != nullptr) {
    const uint32_t pub = kAccPublic;
    VClass* cl2 = nullptr;
    for (VClass* k : all) {
      if (k->descriptor == "Ljava/lang/ClassLoader;") {
        cl2 = k;
        break;
      }
    }
    if (cl2 != nullptr) {
      AddIntrinsicMethod(cl2, "getParent", {}, "Ljava/lang/ClassLoader;", pub, CLGetParent);
      AddIntrinsicMethod(cl2, "getSystemClassLoader", {}, "Ljava/lang/ClassLoader;",
                         kAccPublic | kAccStatic, CLGetSystemClassLoader);
      // Java 9+：平台类加载器。VM 无独立平台 loader，回落 system loader
      // （Charset$ExtendedProviderHolder 的 ServiceLoader 入口依赖）。
      AddIntrinsicMethod(cl2, "getPlatformClassLoader", {}, "Ljava/lang/ClassLoader;",
                         kAccPublic | kAccStatic, CLGetSystemClassLoader);
      AddIntrinsicMethod(cl2, "getResource", {"Ljava/lang/String;"}, "Ljava/net/URL;", pub,
                         CLGetResource);
      AddIntrinsicMethod(cl2, "getResources", {"Ljava/lang/String;"},
                         "Ljava/util/Enumeration;", pub, CLGetResources);
      AddIntrinsicMethod(cl2, "getResourceAsStream", {"Ljava/lang/String;"},
                         "Ljava/io/InputStream;", pub, CLGetResourceAsStream);
      AddIntrinsicMethod(cl2, "getSystemResource", {"Ljava/lang/String;"},
                         "Ljava/net/URL;", kAccPublic | kAccStatic, CLGetSystemResource);
      AddIntrinsicMethod(cl2, "getSystemResources", {"Ljava/lang/String;"},
                         "Ljava/util/Enumeration;", kAccPublic | kAccStatic, CLGetSystemResources);
      AddIntrinsicMethod(cl2, "getSystemResourceAsStream", {"Ljava/lang/String;"},
                         "Ljava/io/InputStream;", kAccPublic | kAccStatic,
                         CLGetSystemResourceAsStream);
    }
  }

  // 统一登记（kInitialized + ClassTable + all_classes）
  for (VClass* k : all) linker->RegisterBuiltinClass(k);
  // 内建注册集 == 派生集（builtin_classes.def）一致性断言（防再犯核心）
  AssertBuiltinRegistration(all, CoreBuiltinClassManifest(), "RegisterBuiltins");
}

// ---- <clinit> 失败 erroneous 语义（docs 03 章 12）----
// 失败类登记（VClass* 为键，只增不清）：后续 active use 经
// EnsureClassInitialized/EnsureInitialized 短路抛 NoClassDefFoundError，
// 不再重跑 <clinit>（重跑 = 静态字面量解密清单原地打转的机制来源）。
namespace {
std::vector<const VClass*>& FailedClinitTable() {
  static std::vector<const VClass*> table;
  return table;
}
}  // namespace

void NoteClinitFailure(VClass* klass) {
  if (klass == nullptr) return;
  std::vector<const VClass*>& table = FailedClinitTable();
  for (const VClass* k : table) {
    if (k == klass) return;
  }
  table.push_back(klass);
}

bool IsClinitFailed(const VClass* klass) {
  if (klass == nullptr) return false;
  for (const VClass* k : FailedClinitTable()) {
    if (k == klass) return true;
  }
  return false;
}


}  // namespace dexvm::runtime
