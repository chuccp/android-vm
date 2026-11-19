// java.lang.System/Runtime/Enum/Float/Double/VMRuntime + StrictMath native 面
// + java.security.AccessController 面。纯移动自 core/builtins.cc（零行为变更）。

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

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "core/class_linker.h"
#include "core/intrinsic.h"
#include "core/framework_builtins_internal.h"  // NewLibObject（lib 组合缝）
#include "core/interpreter.h"
#include "session/class_loader.h"
#include "session/session.h"


#include "core/builtins_internal.h"

namespace dexvm::runtime {

// ---- java.lang.System ----
namespace {
std::unordered_map<std::string, std::u16string>& SysProperties() {
  // VM 虚拟环境（docs 29.2①：虚拟身份类加载前就绪；Android 框架面值对齐设备）。
  // java.vm.name 报告本 VM 自身（dexvm），不冒充 Dalvik：本 VM 的类库是
  // OpenJDK 11 形态（java.lang.invoke/stream 等），按 JVM 能力分支的第三方库
  // 若被 Dalvik 标识误导会退化到缺省内部面的 Android 路径（fastjson2 2.0.66
  // JDKUtils 在 ANDROID 下跳过 IMPL_LOOKUP 初始化，PropertyAccessorFactoryUnsafe
  // 随即 NPE）。Android 设备识别仍由 android.os.Build 等框架面承载。
  static std::unordered_map<std::string, std::u16string> props = {
      // Charset.defaultCharset() = lookup(System.getProperty("file.encoding"))；
      // 缺省缺失会让 new InputStreamReader(is) 抛 "Null charset name"（实测
      // CloudID 设备信息采集链）。VM 文本面恒 UTF-8。
      {"file.encoding", u"UTF-8"},
      {"sun.jnu.encoding", u"UTF-8"},
      {"java.version", u"0"},
      {"java.vm.name", u"dexvm"},
      {"java.vm.version", u"2.1.0"},
      {"java.vm.vendor", u"dexvm"},
      {"java.specification.version", u"0.9"},
      {"java.vendor", u"dexvm"},
      {"os.name", u"Linux"},
      {"os.version", u"5.10.0-android"},
      {"os.arch", u"aarch64"},
      {"file.separator", u"/"},
      {"path.separator", u":"},
      {"line.separator", u"\n"},
      {"user.name", u"root"},
      {"user.home", u"/data"},
      {"user.dir", u"/"},
      {"java.io.tmpdir", u"/data/local/tmp"},
      {"java.library.path", u"/system/lib64:/vendor/lib64"},
      {"java.class.path", u""},
  };
  return props;
}

}  // namespace
Value SysArraycopy(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)n;
  VArray* src = (args[0].ref != nullptr) ? static_cast<VArray*>(args[0].ref) : nullptr;
  int32_t src_pos = args[1].i;
  VArray* dst = (args[2].ref != nullptr) ? static_cast<VArray*>(args[2].ref) : nullptr;
  int32_t dst_pos = args[3].i;
  int32_t length = args[4].i;
  if (src == nullptr || dst == nullptr) {
    ThrowNullPointerException(state, "arraycopy: null array");
    *ok = false;
    return Value{};
  }
  VClass* sk = src->klass;
  VClass* dk = dst->klass;
  if (sk == nullptr || dk == nullptr || !sk->is_array || !dk->is_array) {
    state.ThrowNew("Ljava/lang/ArrayStoreException;", "arraycopy: source or destination not array");
    *ok = false;
    return Value{};
  }
  if (src_pos < 0 || dst_pos < 0 || length < 0 || src_pos > src->length - length ||
      dst_pos > dst->length - length) {
    state.ThrowNew("Ljava/lang/ArrayIndexOutOfBoundsException;",
                   "arraycopy: src=" + std::to_string(src->length) + " srcPos=" +
                       std::to_string(src_pos) + " dst=" + std::to_string(dst->length) +
                       " dstPos=" + std::to_string(dst_pos) + " length=" + std::to_string(length));
    *ok = false;
    return Value{};
  }
  VClass* sc = sk->component_type;
  VClass* dc = dk->component_type;
  if (sc == nullptr || dc == nullptr) {
    state.ThrowNew("Ljava/lang/ArrayStoreException;", "arraycopy: bad component");
    *ok = false;
    return Value{};
  }
  if (sc->is_primitive || dc->is_primitive) {
    // 基本类型数组：同型拷贝（JVM：异型 ArrayStoreException）
    if (sc->primitive_type != dc->primitive_type) {
      state.ThrowNew("Ljava/lang/ArrayStoreException;",
                     std::string("arraycopy: type mismatch: ") + sk->descriptor + " -> " +
                         dk->descriptor);
      *ok = false;
      return Value{};
    }
  }
  for (int32_t i = 0; i < length; i++) {
    ObjHeader* e = ArrRef(src, src_pos + i);
    if (!sc->is_primitive && e != nullptr && e->klass != nullptr && !dc->IsAssignableFrom(e->klass)) {
      state.ThrowNew("Ljava/lang/ArrayStoreException;",
                     (e->klass->descriptor + " into " + dk->descriptor));
      *ok = false;
      return Value{};
    }
    ArrSetRef(dst, dst_pos + i, e);
  }
  return Value{};
}
Value SysCurrentTimeMillis(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.l = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
  return r;
}
Value SysNanoTime(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.l = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
  return r;
}
Value SysIdentityHashCode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  ObjHeader* o = args[0].ref;
  r.i = o == nullptr ? 0 : static_cast<int32_t>((reinterpret_cast<uintptr_t>(o) >> 3) & 0x7fffffffu);
  return r;
}

// ---- java.lang.Float/Double 位转换 native（ART java_lang_Float/Double.cpp 等价面）。
// VM 值模型中 float/double 以【原始位型】存于 Value 槽（GetF/GetD memcpy 位型），
// 参数铺陈（DoCall）对 F 走 GetI、对 D 走 GetL，均透传原始位。故四位转换 =
// 【位型恒等】（floatToRawIntBits 保 NaN 位；floatToIntBits 归一化 NaN 由 Java 层
// 调 floatToRawIntBits 前自行判 NaN）。这些是 java.lang.* 标准 intrinsic（非考卷特判），
// 经 BindWellKnownNatives 绑到 vendored Float/Double 的 native 槽。----
Value FloatFloatToRawIntBits(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state; (void)self;
  *ok = true;
  Value r{};
  r.i = n >= 1 ? args[0].i : 0;  // float 位型 -> int 位型（恒等）
  return r;
}
Value FloatIntBitsToFloat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self;
  *ok = true;
  Value r{};
  r.i = n >= 1 ? args[0].i : 0;  // int 位型 -> float 位型（恒等）
  return r;
}
Value DoubleDoubleToRawLongBits(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)state; (void)self;
  *ok = true;
  Value r{};
  r.l = n >= 1 ? args[0].l : 0;  // double 位型 -> long 位型（恒等）
  return r;
}
Value DoubleLongBitsToDouble(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state; (void)self;
  *ok = true;
  Value r{};
  r.l = n >= 1 ? args[0].l : 0;  // long 位型 -> double 位型（恒等）
  return r;
}
Value SysGetProperty(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string key = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  auto& props = SysProperties();
  auto it = props.find(key);
  if (it != props.end()) return MakeStr(state, it->second, ok);
  if (n > 1) return args[1];  // 默认值
  return Value{};  // null：未设置
}
// System.getProperties()：lib java.util.Properties 实例，逐项填充 VM 属性表
// （与 getProperty 口径一致；OpenJDK ProcessImpl/GetPropertyAction 等消费面）。
Value SysGetProperties(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  bool lib_ok = true;
  VObject* props = NewLibObject(state, "Ljava/util/Properties;", {}, nullptr, 0, &lib_ok);
  if (props == nullptr) {
    state.ClearException();
    return r;
  }
  VMethod* put = nullptr;
  for (VClass* k = props->klass; k != nullptr && put == nullptr; k = k->super_class) {
    for (auto& m : k->virtual_methods) {
      if (m.name == "put" && m.param_types.size() == 2 &&
          m.param_types[0] == "Ljava/lang/Object;" &&
          m.param_types[1] == "Ljava/lang/Object;") {
        put = &m;
        break;
      }
    }
  }
  if (put != nullptr) {
    for (const auto& kv : SysProperties()) {
      Value call[3] = {};
      call[0].ref = reinterpret_cast<ObjHeader*>(props);
      std::string key(kv.first.begin(), kv.first.end());
      call[1].ref = reinterpret_cast<ObjHeader*>(
          state.linker->AllocString(std::u16string(key.begin(), key.end())));
      call[2].ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(kv.second));
      Value ret{};
      InvokeMethod(state, put, call, 3, &ret);
      if (state.HasPendingException()) state.ClearException();
    }
  }
  r.ref = reinterpret_cast<ObjHeader*>(props);
  return r;
}
Value SysSetProperty(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string key = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  std::u16string val = (n > 1 && args[1].ref != nullptr)
                           ? std::u16string(static_cast<VString*>(args[1].ref)->chars(),
                                            static_cast<VString*>(args[1].ref)->chars() +
                                                static_cast<VString*>(args[1].ref)->length)
                           : std::u16string();
  auto& props = SysProperties();
  Value old{};
  auto it = props.find(key);
  if (it != props.end()) {
    old = MakeStr(state, it->second, ok);
    if (!*ok) return Value{};
    it->second = val;
  } else {
    props[key] = val;
  }
  return old;
}
Value SysClearProperty(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string key = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  auto& props = SysProperties();
  auto it = props.find(key);
  Value old{};
  if (it != props.end()) {
    old = MakeStr(state, it->second, ok);
    if (!*ok) return Value{};
    props.erase(it);
  }
  return old;
}
Value SysGetenvName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string key = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  const char* v = std::getenv(key.c_str());
  if (v == nullptr) return Value{};
  std::u16string out;
  AppendUtf16(&out, v);
  return MakeStr(state, out, ok);
}
Value SysLineSeparator(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  return MakeStr(state, u"\n", ok);
}
Value SysMapLibraryName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string name = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  std::u16string out;
  AppendUtf16(&out, ("lib" + name + ".so").c_str());
  return MakeStr(state, out, ok);
}

// loadLibrary/load 公共语义（docs 07 章 16.2）：交给宿主 native 库加载器；
// 无加载器/加载失败 -> UnsatisfiedLinkError（不伪造成功）。
// JNI_OnLoad/RegisterNatives 抛出的 guest 异常按原样上抛（不包
// UnsatisfiedLinkError，诊断保真）——LibraryLoadHook 契约面。
// VM 内建 native 面库例外（Q2 缝）：java.net socket 面等平台库的 native 槽由
// BindWellKnownNatives 直接登记（无宿主 .so 可 dlopen）——这类库名的
// loadLibrary 语义 = 库已随 VM 就位（native 面已注册），no-op 成功。清单与
// kKnown 表的 java.* native 面同步维护（同 TU 单一事实源）。
namespace {
bool IsVmBuiltinNativeFaceLib(const std::string& libname) {
  // "libnet.so" → "net"（SysLoadLibrary 拼装形；裸名/路径均归一化到核名）
  std::string core = libname;
  if (core.rfind("lib", 0) == 0) core = core.substr(3);
  if (core.size() > 3 && core.compare(core.size() - 3, 3, ".so") == 0) {
    core = core.substr(0, core.size() - 3);
  }
  static const char* kVmFaceLibs[] = {
      "net",  // java.net socket/DNS native 面（kKnown java.net 节）
  };
  for (const char* n : kVmFaceLibs) {
    if (core == n) return true;
  }
  return false;
}

Value LoadLibraryCommon(VMState& state, const std::string& name, bool* ok) {
  if (IsVmBuiltinNativeFaceLib(name)) {
    return Value{};  // VM 内建面：native 槽已由 BindWellKnownNatives 登记
  }
  LibraryLoadHook hook = g_library_load_hook;
  std::string error;
  bool loaded = hook != nullptr && hook(name, &error);
  if (state.HasPendingException()) {
    *ok = false;
    return Value{};
  }
  if (loaded) return Value{};
  if (error.empty()) {
    error = "couldn't find " + name;
  }
  state.ThrowNew("Ljava/lang/UnsatisfiedLinkError;", error);
  *ok = false;
  return Value{};
}
}  // namespace
Value SysLoadLibrary(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string name = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  return LoadLibraryCommon(state, "lib" + name + ".so", ok);
}
Value SysLoad(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string path = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  return LoadLibraryCommon(state, path, ok);
}
Value SysGc(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  return Value{};  // GC 由 VM 自主调度（docs 02 章）
}

// ---- java.lang.Runtime ----
Value RuntimeGetRuntime(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  VClass* k = state.linker->FindClass("Ljava/lang/Runtime;");
  if (k == nullptr || k->static_storage.empty()) {
    state.ThrowNew("Ljava/lang/InternalError;", "Runtime missing");
    *ok = false;
    return Value{};
  }
  if (k->static_storage[0].ref == nullptr) {
    VObject* rt = state.linker->AllocObject(k);
    if (rt == nullptr) {
      ThrowOOM(state, "Runtime.getRuntime");
      *ok = false;
      return Value{};
    }
    k->static_storage[0].ref = rt;
  }
  Value r{};
  r.ref = k->static_storage[0].ref;
  return r;
}
Value RuntimeLoadLibrary(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string name = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  return LoadLibraryCommon(state, "lib" + name + ".so", ok);
}
Value RuntimeLoad(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string path = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  return LoadLibraryCommon(state, path, ok);
}
Value RuntimeAvailableProcessors(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = 4;  // VM 容器默认核数（宿主核数对 guest 不可见语义后置）
  return r;
}
Value RuntimeTotalMemory(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n; (void)ok;
  // Android/OpenJDK 语义：totalMemory = 当前堆可用总量（footprint），
  // freeMemory = total - used（不变式 free <= total 恒成立）。此前映射
  // total=bytes_allocated、free=capacity-allocated 违反不变式，已修正。
  Value r{};
  r.l = state.heap != nullptr ? static_cast<int64_t>(state.heap->capacity()) : 0;
  return r;
}
Value RuntimeMaxMemory(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.l = state.heap != nullptr ? static_cast<int64_t>(state.heap->capacity()) : 0;
  return r;
}
Value RuntimeFreeMemory(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  if (state.heap != nullptr) {
    size_t total = state.heap->capacity();
    size_t used = state.heap->bytes_allocated();
    r.l = static_cast<int64_t>(total > used ? total - used : 0);
  }
  return r;
}
Value RuntimeGc(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return SysGc(state, self, args, n, ok);
}
// Runtime.exec：VM 无外部进程能力 -> lib java.lang.VmProcess 哑进程（空输出、
// 立即退出）。崩溃采集/设备信息等调用方按"命令无输出"自然降级。
Value RuntimeExec(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  bool lib_ok = true;
  VObject* proc = NewLibObject(state, "Ljava/lang/VmProcess;", {}, nullptr, 0, &lib_ok);
  if (proc == nullptr) state.ClearException();
  r.ref = reinterpret_cast<ObjHeader*>(proc);
  return r;
}
// ---- java.lang.Runtime$Version ----
// JarFile.<clinit>（java.util.jar，JarVerifier 校验路径）需要
// Runtime.version().feature() 与 Runtime.Version.parse("8").feature()。
// Runtime 为内建类 -> lib Runtime.java 不入 runtime.dex，其嵌套 Version 缺失；
// 这里按内建类承接（字段：feature@0、minor@8、security@16、original@24）。
namespace {
struct RuntimeVersionParts {
  int feature = 0;
  int minor = 0;
  int security = 0;
};

// OpenJDK Version.parse 的数值形态子集：1~4 段点分十进制（无 pre/build 后缀）。
bool ParseRuntimeVersionString(const std::string& s, RuntimeVersionParts* out) {
  if (s.empty() || s.size() > 64) return false;
  int parts[4] = {0, 0, 0, 0};
  int count = 0;
  size_t i = 0;
  for (;;) {
    if (count == 4) return false;
    size_t start = i;
    int64_t v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      v = v * 10 + (s[i] - '0');
      if (v > 0x7fffffffLL) return false;
      i++;
    }
    if (i == start) return false;
    parts[count++] = static_cast<int>(v);
    if (i == s.size()) break;
    if (s[i] != '.') return false;
    i++;
    if (i == s.size()) return false;
  }
  out->feature = parts[0];
  out->minor = count > 1 ? parts[1] : 0;
  out->security = count > 2 ? parts[2] : 0;
  return true;
}

VObject* AllocRuntimeVersion(VMState& state, const RuntimeVersionParts& p,
                             const std::string& original, bool* ok) {
  VClass* k = state.linker != nullptr ? state.linker->FindClass("Ljava/lang/Runtime$Version;")
                                      : nullptr;
  if (k == nullptr) {
    state.ThrowNew("Ljava/lang/NoClassDefFoundError;", "java/lang/Runtime$Version");
    *ok = false;
    return nullptr;
  }
  VObject* v = state.linker->AllocObject(k);
  if (v == nullptr) {
    ThrowOOM(state, "Runtime$Version allocation failed");
    *ok = false;
    return nullptr;
  }
  SetIField(v, 0, p.feature);
  SetIField(v, 8, p.minor);
  SetIField(v, 16, p.security);
  VField* orig = FindInstField(k, "original");
  if (orig != nullptr && state.linker != nullptr) {
    VString* str = state.linker->AllocString(
        std::u16string(original.begin(), original.end()));
    ObjHeader* h = reinterpret_cast<ObjHeader*>(str);
    std::memcpy(v->field_data() + orig->offset, &h, sizeof(h));
  }
  return v;
}
}  // namespace

Value RuntimeVersion(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  // Java 8 语义基线：Android 无 Runtime.version 面；8 使 JarFile 走 base 版本条目
  // （multi-release 判定关闭），与 libcore StrictJarFile 对 §8 的处理一致。
  RuntimeVersionParts p;
  p.feature = 8;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(AllocRuntimeVersion(state, p, "8", ok));
  return r;
}
Value VersionParse(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "version string");
    *ok = false;
    return Value{};
  }
  std::string s = Mutf8Arg(args[0]);
  RuntimeVersionParts p;
  if (!ParseRuntimeVersionString(s, &p)) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;",
                   "Invalid version string: '" + s + "'");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(AllocRuntimeVersion(state, p, s, ok));
  return r;
}
Value VersionFeature(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = GetIField(self, 0);
  return r;
}
Value VersionMajor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return VersionFeature(state, self, args, n, ok);
}
Value VersionMinor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = GetIField(self, 8);
  return r;
}
Value VersionSecurity(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = GetIField(self, 16);
  return r;
}
Value VersionToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  Value r{};
  VField* f = FindInstField(self->klass, "original");
  if (f != nullptr) {
    std::memcpy(&r.ref, self->field_data() + f->offset, sizeof(r.ref));
    if (r.ref != nullptr) return r;
  }
  return MakeStr(state, u"", ok);
}
Value VersionEquals(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)ok;
  Value r{};
  r.i = 0;
  ObjHeader* other = (n > 0) ? args[0].ref : nullptr;
  if (other == nullptr || other->klass == nullptr ||
      other->klass->descriptor != "Ljava/lang/Runtime$Version;") {
    return r;
  }
  VObject* o = static_cast<VObject*>(other);
  r.i = (GetIField(self, 0) == GetIField(o, 0) &&
         GetIField(self, 8) == GetIField(o, 8) &&
         GetIField(self, 16) == GetIField(o, 16)) ? 1 : 0;
  return r;
}
Value VersionHashCode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = (GetIField(self, 0) * 31 + GetIField(self, 8)) * 31 + GetIField(self, 16);
  return r;
}
Value VersionCompareTo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "version");
    *ok = false;
    return Value{};
  }
  VObject* o = static_cast<VObject*>(args[0].ref);
  Value r{};
  r.i = 0;
  for (uint32_t off : {0u, 8u, 16u}) {
    int32_t a = GetIField(self, off);
    int32_t b = GetIField(o, off);
    if (a != b) {
      r.i = a < b ? -1 : 1;
      break;
    }
  }
  return r;
}
// ---- java.lang.Enum ----
Value EnumInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)ok;
  // protected Enum(String name, int ordinal)：name @0、ordinal @8
  VField* nf = FindInstField(self->klass, "name");
  VField* of = FindInstField(self->klass, "ordinal");
  if (nf != nullptr) {
    ObjHeader** slot = reinterpret_cast<ObjHeader**>(self->field_data() + nf->offset);
    *slot = (n > 0) ? args[0].ref : nullptr;
  }
  if (of != nullptr) {
    // 8 字节槽全宽写（kWord 零扩展）：半槽写会把旧指针高 32 位留在槽里
    // （脏引用 0xHHHHHHHH00000000 形态；对照 gc.cc IsHeapStart 防御）
    uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(args[1].i));
    std::memcpy(self->field_data() + of->offset, &slot, sizeof(slot));
  }
  (void)state;
  return Value{};
}
Value EnumName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  Value r{};
  VField* nf = FindInstField(self->klass, "name");
  if (nf != nullptr) {
    std::memcpy(&r.ref, self->field_data() + nf->offset, sizeof(r.ref));
  }
  (void)state;
  return r;
}
Value EnumOrdinal(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  VField* of = FindInstField(self->klass, "ordinal");
  if (of != nullptr) {
    int32_t v = 0;
    std::memcpy(&v, self->field_data() + of->offset, sizeof(v));
    r.i = v;
#if defined(__ANDROID__)
    // TEMP DIAG: Jackson MapperFeature 掩码（USE_ANNOTATIONS 关闭归因；事后移除）
    if (self->klass != nullptr &&
        self->klass->descriptor == "Lcom/fasterxml/jackson/databind/o;") {
      static int mf_logs = 0;  // 降噪（高频枚举路径）
      if (mf_logs++ < 50) {
        __android_log_print(ANDROID_LOG_INFO, "DexVmAnno", "MapperFeature.ordinal=%d", v);
      }
    }
#endif
  }
  return r;
}
Value EnumToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  Value r{};
  VField* nf = FindInstField(self->klass, "name");
  if (nf != nullptr) {
    std::memcpy(&r.ref, self->field_data() + nf->offset, sizeof(r.ref));
    if (r.ref != nullptr) return r;
  }
  return MakeStr(state, u"", ok);
}
Value EnumEquals(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = (n > 0 && args[0].ref == self) ? 1 : 0;
  return r;
}
Value EnumHashCode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  Value r{};
  r.i = self == nullptr ? 0
                        : static_cast<int32_t>((reinterpret_cast<uintptr_t>(self) >> 3) &
                                                0x7fffffffu);
  (void)state;
  return r;
}
Value EnumCompareToEnum(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)ok;
  Value a = EnumOrdinal(state, self, nullptr, 0, ok);
  Value b = EnumOrdinal(state, static_cast<VObject*>(args[0].ref), nullptr, 0, ok);
  Value r{};
  r.i = a.i < b.i ? -1 : (a.i == b.i ? 0 : 1);
  return r;
}
Value EnumGetDeclaringClass(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = self->klass;
  // 具体常量匿名子类：声明类为父枚举类
  if (k != nullptr && k->super_class != nullptr && k->super_class->super_class != nullptr &&
      k->super_class->super_class->descriptor == "Ljava/lang/Enum;") {
    k = k->super_class;
  }
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(k);
  return r;
}
// Enum.clone 恒抛 CloneNotSupportedException（Java：final + throw，语义）
Value EnumClone(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  state.ThrowNew("Ljava/lang/CloneNotSupportedException;", "clone not supported for enum");
  *ok = false;
  return Value{};
}
Value EnumValueOf(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  // static <T extends Enum> T valueOf(Class<T>, String)：按枚举常量名匹配
  VClass* k = reinterpret_cast<VClass*>(args[0].ref);
  std::string name = (n > 1 && args[1].ref != nullptr) ? Mutf8Arg(args[1]) : "";
  if (k != nullptr) {
    for (auto& f : k->static_fields) {
      if (f.type_descriptor != k->descriptor || f.offset >= k->static_storage.size()) continue;
      ObjHeader* o = k->static_storage[f.offset].ref;
      if (o == nullptr) continue;
      Value nv = EnumName(state, static_cast<VObject*>(o), nullptr, 0, ok);
      if (nv.ref != nullptr && static_cast<VString*>(nv.ref)->ToModifiedUtf8() == name) {
        Value r{};
        r.ref = o;
        return r;
      }
    }
  }
  state.ThrowNew("Ljava/lang/IllegalArgumentException;",
                 "No enum constant " + (k != nullptr ? ClassName(k) : "?") + "." + name);
  *ok = false;
  return Value{};
}
// ---- dalvik.system.VMRuntime hidden API 面（docs 29.2③：不设真实限制）----
Value VMRuntimeGetRuntime(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  VClass* k = state.linker->FindClass("Ldalvik/system/VMRuntime;");
  if (k == nullptr || k->static_storage.empty()) {
    state.ThrowNew("Ljava/lang/InternalError;", "VMRuntime missing");
    *ok = false;
    return Value{};
  }
  if (k->static_storage[0].ref == nullptr) {
    VObject* rt = state.linker->AllocObject(k);
    if (rt == nullptr) {
      ThrowOOM(state, "VMRuntime.getRuntime");
      *ok = false;
      return Value{};
    }
    k->static_storage[0].ref = rt;
    // targetHeapUtilization 默认（ART：0.75）
    VField* uf = FindInstField(k, "targetHeapUtilization");
    if (uf != nullptr) {
      double d = 0.75;
      std::memcpy(rt->field_data() + uf->offset, &d, sizeof(d));
    }
  }
  Value r{};
  r.ref = k->static_storage[0].ref;
  return r;
}
Value VMRuntimeSetHiddenApiExemptions(VMState& state, VObject* self, const Value* args, size_t n,
                                      bool* ok) {
  (void)state; (void)ok;
  // Android 语义：设置豁免前缀；本 VM 不实施 hidden API 限制（不设真实限制），
  // 仅记录状态供诊断观察面使用。
  VField* f = FindInstField(self->klass, "hiddenApiExemptions");
  if (f != nullptr && n > 0) {
    ObjHeader** slot = reinterpret_cast<ObjHeader**>(self->field_data() + f->offset);
    *slot = args[0].ref;
  }
  return Value{};
}
Value VMRuntimeSetHiddenApiAccessLogSampling(VMState& state, VObject* self, const Value* args,
                                             size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  return Value{};  // 采样率设置：无 hidden API 访问日志面，语义为记录即弃
}
Value VMRuntimeGetTargetHeapUtilization(VMState& state, VObject* self, const Value* args,
                                        size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.d = 0.75;
  VField* f = FindInstField(self->klass, "targetHeapUtilization");
  if (f != nullptr) std::memcpy(&r.d, self->field_data() + f->offset, sizeof(r.d));
  return r;
}
Value VMRuntimeSetTargetHeapUtilization(VMState& state, VObject* self, const Value* args, size_t n,
                                       bool* ok) {
  (void)state; (void)ok;
  VField* f = FindInstField(self->klass, "targetHeapUtilization");
  if (f != nullptr && n > 0) {
    std::memcpy(self->field_data() + f->offset, &args[0].d, sizeof(double));
  }
  return Value{};
}
// lib java.lang.System.getSecurityManager()：无 SecurityManager（恒 null）
Value SysGetSecurityManager(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

// java.security.AccessController native 面：doPrivileged(action[, ctx[, perms]]) =
// action.run()（无 SecurityManager：特权栈为空，直接执行）。
Value ACDoPrivileged(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  if (n < 1 || IsNullArg(args[0])) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "doPrivileged action == null");
    *ok = false;
    return Value{};
  }
  Value run_r{};
  if (!InvokeVirtualNamed(state, static_cast<VObject*>(args[0].ref), "run", {},
                          "Ljava/lang/Object;", nullptr, 0, &run_r)) {
    return Value{};  // 异常传递（含 PrivilegedExceptionAction.run 抛出面）
  }
  *ok = true;
  return run_r;
}

Value ACNoContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};  // getStackAccessControlContext/getInherited... → null
}
// java.lang.* 标准 native 方法 -> intrinsic 绑定（libcore JNI 等价面）。
// vendored java.lang.Float/Double 的位转换方法声明为 native（无码、无 JNI 注册），
// 这里按 (descriptor, name, 签名) 绑到 VM intrinsic。通用登记表（非考卷特判）：
// ART 对这些 java.lang.* 位转换同样走 intrinsic（java_lang_Float/Double.cpp）。
// 只绑【native 且无码且未绑定】的槽（builtin 已有 intrinsic 者不动）。
// ---- java.lang.StrictMath native 面（fdlibm 形态；BindWellKnownNatives 绑定）----
// §31.7 A 档简单热路径：libm 单调用直通（<100 行/面、热路径）。BigInteger/Math
// 等经 lib Math -> StrictMath.log 等进来；未绑定时 UnsatisfiedLinkError。
Value MathSin(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::sin(args[0].d);
  return r;
}
Value MathCos(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::cos(args[0].d);
  return r;
}
Value MathTan(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::tan(args[0].d);
  return r;
}
Value MathAsin(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::asin(args[0].d);
  return r;
}
Value MathAcos(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::acos(args[0].d);
  return r;
}
Value MathAtan(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::atan(args[0].d);
  return r;
}
Value MathAtan2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::atan2(args[0].d, args[1].d);
  return r;
}
Value MathExp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::exp(args[0].d);
  return r;
}
Value MathLog(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::log(args[0].d);
  return r;
}
Value MathLog10(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::log10(args[0].d);
  return r;
}
Value MathLog1p(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::log1p(args[0].d);
  return r;
}
Value MathExpm1(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::expm1(args[0].d);
  return r;
}
Value MathSqrt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::sqrt(args[0].d);
  return r;
}
Value MathSinh(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::sinh(args[0].d);
  return r;
}
Value MathCosh(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::cosh(args[0].d);
  return r;
}
Value MathTanh(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::tanh(args[0].d);
  return r;
}
Value MathIEEERemainder(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)n;
  *ok = true;
  Value r{};
  r.d = std::remainder(args[0].d, args[1].d);
  return r;
}

}  // namespace dexvm::runtime
