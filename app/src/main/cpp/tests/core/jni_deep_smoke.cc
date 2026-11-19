// JNI 桥接语义压力冒烟（tools/jni_deep_tests.sh）——SecNeo 解密回调形态。
// 排查方针（用户定性）：应用不会合法深递归（真机早炸）；深栈/不终止循环 =
// 语义偏差实证。用例按【带终止条件的正常调用循环】构造（正确语义应有限圈
// 结束），复现出不终止/栈漂移/误读即语义 bug 实证：
//   1) 对象链终止循环：CallObjectMethodV(next) 走到 null 必须停（该 null 却
//      非空 = 死循环伪递归）；每圈跨 JNI 边界，栈漂移必须为零（累积 = 套娃）
//   2) 字符串语义回环：长度/MUTF-8 字节内容/NewStringUTF 回环逐字节对齐
//      Android（空串/中文/内嵌 NUL/增补平面）——内容偏差 = 终止失效
//   3) 接口派发终止循环（FindInterfaceImpl 派发回环怀疑面）
//   4) local ref 代回绕误判（悬挂 jobject = 纯语义，与栈无关）
//   5) local ref 耗尽语义（null + OutOfMemoryError）
//   6) 深递归护栏（配套缓解验证：SOE 可观测停点，非根因结论）
//   7) 调度器预算中止归因（非信号失败可归因，不报 "unknown signal"）
// 用法: jni_deep_smoke <fixture.dex>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "jni/handle_table.h"
#include "jni/jni_env.h"
#include "jni/native_manager.h"
#include "memory/heap.h"

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
      g_failures++;                                               \
    } else {                                                      \
      std::printf("PASS: %s\n", msg);                             \
    }                                                             \
  } while (0)

using dexvm::runtime::ClassLinker;
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;
using dexvm::runtime::VMState;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;

std::vector<uint8_t> ReadFile(const char* path) {
  std::vector<uint8_t> out;
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) return out;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  out.resize(static_cast<size_t>(n));
  if (n > 0 && std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
  std::fclose(f);
  return out;
}

// ===================== 测试 native（SecNeo 回调形态）=====================

VMState* g_state = nullptr;
JNIEnv* g_env = nullptr;
jobject g_self = nullptr;
jmethodID g_step_mid = nullptr;
jmethodID g_next_mid = nullptr;      // Node.next()Ljava/lang/DeepChain$Node;
jmethodID g_cursor_next_mid = nullptr;  // Cursor.next()Ljdtest/DeepChain$Cursor;
long long g_cycles = 0;
long long g_stack_drift = 0;  // 循环内 C++ 栈水位下移峰值（0 = 无累积套娃）

jobject CallObjectV(JNIEnv* e, jobject o, jmethodID mid, ...) {
  va_list ap;
  va_start(ap, mid);
  jobject r = e->functions->CallObjectMethodV(e, o, mid, ap);
  va_end(ap);
  return r;
}

// ---- 返回句柄新 slot 单调断言（真 ART IndirectRef 形态，oracle 终审对照）----
// 每笔对象返回必须是【新槽】（slot 号严格递增）；陈旧/复用槽 = D2 实证。
uint32_t g_last_ret_slot = 0;
bool g_slot_mono_ok = true;

bool NoteRetSlot(jobject h) {
  if (h == nullptr) return true;
  uint32_t idx = 0, gen = 0;
  if (!dexvm::jni::DecodeHandle(
          static_cast<dexvm::jni::Handle>(reinterpret_cast<uintptr_t>(h)), &idx, &gen)) {
    g_slot_mono_ok = false;
    return false;
  }
  if (idx <= g_last_ret_slot) {
    g_slot_mono_ok = false;
    return false;
  }
  g_last_ret_slot = idx;
  return true;
}

jobject CallStaticObjectV(JNIEnv* e, jclass c, jmethodID mid, ...) {
  va_list ap;
  va_start(ap, mid);
  jobject r = e->functions->CallStaticObjectMethodV(e, c, mid, ap);
  va_end(ap);
  return r;
}

long long CallStaticLongV(JNIEnv* e, jclass c, jmethodID mid, ...) {
  va_list ap;
  va_start(ap, mid);
  long long r = e->functions->CallStaticLongMethodV(e, c, mid, ap);
  va_end(ap);
  return r;
}

// ---- 1) 对象链终止循环（native 循环 -> guest next -> native ping -> 回）----
// 正确语义：有限圈（expect 圈）后 next() 返 null，循环必须终止；栈水位每圈
// 复位（漂移 = 每圈累积 = 语义套娃实证）。防真死循环：1e6 圈强制返回 -1。
jlong JNICALL n_nativeLoop(JNIEnv* env, jclass, jobject head, jint expect) {
  (void)expect;
  jobject node = head;
  jlong count = 0;
  uintptr_t sp0 = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  uintptr_t spmin = sp0;
  while (node != nullptr) {
    if (count > 1000000) return -1;  // 不终止（语义偏差实证）
    uintptr_t sp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    if (sp < spmin) spmin = sp;
    jobject nxt = CallObjectV(env, node, g_next_mid);
    if (env->functions->ExceptionCheck(env)) return -2;
    if (nxt != nullptr && !NoteRetSlot(nxt)) return -3;  // 返回句柄陈旧槽（D2）
    if (node != head) env->functions->DeleteLocalRef(env, node);
    node = nxt;
    count++;
  }
  g_stack_drift = static_cast<long long>(sp0 - spmin);
  return count;
}

jlong JNICALL n_nativePing(JNIEnv*, jclass, jlong v) { return v; }

// ---- 3) 接口派发终止循环（接口 methodID -> FindInterfaceImpl 派发）----
jlong JNICALL n_nativeIfaceLoop(JNIEnv* env, jclass, jobject head, jint expect) {
  (void)expect;
  jobject node = head;
  jlong count = 0;
  uintptr_t sp0 = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  uintptr_t spmin = sp0;
  while (node != nullptr) {
    if (count > 1000000) return -1;
    uintptr_t sp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    if (sp < spmin) spmin = sp;
    jobject nxt = CallObjectV(env, node, g_cursor_next_mid);
    if (env->functions->ExceptionCheck(env)) return -2;
    if (node != head) env->functions->DeleteLocalRef(env, node);
    node = nxt;
    count++;
  }
  long long drift = static_cast<long long>(sp0 - spmin);
  if (drift > g_stack_drift) g_stack_drift = drift;
  return count;
}

// ---- 2) 字符串语义回环（对齐 Android MUTF-8 语义，逐字节）----
struct StrCase {
  const char* factory;  // DeepChain 工厂方法名
  int32_t utf16_units;
  const char* mutf8;  // 期望 MUTF-8 字节（GetStringUTFChars 逐字节一致）
  size_t mutf8_len;
};

jlong JNICALL n_nativeStrProbe(JNIEnv* env, jclass cls, jint which) {
  static const StrCase kCases[] = {
      {"mkAscii", 2, "ok", 2},
      {"mkEmpty", 0, "", 0},
      {"mkCn", 3, "\xe4\xb8\xad\xe6\x96\x87\xe4\xb8\xb2", 9},  // 中文串
      {"mkNul", 3, "a\xc0\x80" "b", 4},                        // 内嵌 NUL = C0 80
      {"mkSupp", 2, "\xed\xa0\xbd\xed\xb8\x80", 6},            // U+1F600 CESU-8
  };
  if (which < 0 || which >= 5) return -1;
  const StrCase& c = kCases[which];
  long long bad = 0;
  jmethodID mid = g_env->functions->GetStaticMethodID(env, cls, c.factory, "()Ljava/lang/String;");
  jstring s = static_cast<jstring>(CallStaticObjectV(env, cls, mid));
  if (s == nullptr) return -2;
  jsize units = env->functions->GetStringLength(env, s);
  jsize blen = env->functions->GetStringUTFLength(env, s);
  const char* utf = env->functions->GetStringUTFChars(env, s, nullptr);
  if (units != c.utf16_units) bad++;
  if (blen != static_cast<jsize>(c.mutf8_len)) bad++;
  if (utf == nullptr || std::memcmp(utf, c.mutf8, c.mutf8_len) != 0) bad++;
  if (utf != nullptr) env->functions->ReleaseStringUTFChars(env, s, utf);
  // NewStringUTF 回环（Mutf8ToChars 解码语义）：同样内容必须逐字节还原
  jstring r = env->functions->NewStringUTF(env, c.mutf8);
  if (r == nullptr) return -2;
  jsize runits = env->functions->GetStringLength(env, r);
  const char* rutf = env->functions->GetStringUTFChars(env, r, nullptr);
  if (runits != c.utf16_units) bad++;
  if (rutf == nullptr || std::memcmp(rutf, c.mutf8, c.mutf8_len) != 0) bad++;
  if (rutf != nullptr) env->functions->ReleaseStringUTFChars(env, r, rutf);
  env->functions->DeleteLocalRef(env, r);
  env->functions->DeleteLocalRef(env, s);
  return bad;
}

// ---- 4) local ref 代回绕探针 ----
jlong JNICALL n_nativeWrap(JNIEnv* env, jclass, jlong iters) {
  jstring keep = env->functions->NewStringUTF(env, "STALE-MARKER-XYZZY-0123456789");
  env->functions->DeleteLocalRef(env, keep);
  for (jlong i = 0; i + 1 < iters; i++) {
    jstring h = env->functions->NewStringUTF(env, "x");
    if (h == nullptr) break;
    env->functions->DeleteLocalRef(env, h);
  }
  jstring live = env->functions->NewStringUTF(env, "y");
  jsize misread = env->functions->GetStringLength(env, keep);  // 过期句柄读
  if (env->functions->ExceptionCheck(env)) env->functions->ExceptionClear(env);
  if (live != nullptr) env->functions->DeleteLocalRef(env, live);
  return misread;  // 0 = 未误判；>0 = 代回绕误读
}

// ---- 5) local ref 耗尽语义 ----
jlong JNICALL n_nativeExhaust(JNIEnv* env, jclass) {
  jclass objCls = env->functions->FindClass(env, "java.lang.Object");
  jobject keep = env->functions->AllocObject(env, objCls);
  jlong n = 0;
  while (env->functions->NewLocalRef(env, keep) != nullptr) {
    n++;
    if (n > 100000) break;
  }
  jboolean oom = env->functions->ExceptionCheck(env);
  if (oom) env->functions->ExceptionClear(env);
  if (keep != nullptr) env->functions->DeleteLocalRef(env, keep);
  return oom ? n : -n;
}

// ---- 6) 深链 native（配套缓解验证）----
jstring JNICALL n_nativeStep(JNIEnv* env, jobject self, jlong depth, jstring s) {
  g_cycles++;
  if (self == nullptr || s == nullptr) return nullptr;  // 耗尽/误判哨兵：见 null 即收口
  const char* utf = env->functions->GetStringUTFChars(env, s, nullptr);
  if (utf != nullptr) env->functions->ReleaseStringUTFChars(env, s, utf);
  for (int k = 0; k < 2; k++) {
    jstring t = env->functions->NewStringUTF(env, "c");
    if (t != nullptr) env->functions->DeleteLocalRef(env, t);
  }
  if (depth <= 0) return s;
  if (env->functions->ExceptionCheck(env)) return nullptr;
  jobject r = CallObjectV(env, self, g_step_mid, depth - 1, s);
  return static_cast<jstring>(r);
}

// ---- 8) 真形态 H.d 四步 native（golden 对照 + 缓存命中快路径 + GetMethodID 一次）----
// golden enc->plain 对：/tmp/zhihu_oracle/logs/forensic8.stdout（真机 8454 圈
// trace）逐对提取（NewStringUTF 参数的 ASCII 前缀 = 解密明文）。
struct GoldenPair {
  const char* enc;
  const char* plain;
};
const GoldenPair kGoldenPairs[] = {
    {"G6C82C716A6", "early"},
    {"G6085FB15AB1DAA3DE506", "ifNotMatch"},
    {"G6586C31FB3", "level"},
    {"G6490D2", "msg"},
    {"G678CDB1F", "none"},
    {"G7A88C025BC3FA624E30084", "sku_comment"},
    {"G4482DB0FBE3C992CE107835CF7F7F3DB689AD0089B35B82CF4079144FBFFC6C5",
     "ManualRegisterPlayerDeserializer"},
    {"G798FD403BA22EB3CF5079E4FB2C6CCDA798AD91F8B39A62CA6248347FCC1C6C46C91DC1BB339B12CF4",
     "player using CompileTime JsonDeserializer"},
    {"G408DC60EBE3EA82CD61C9F5EFBE1C6C52784D00EF711A82AE91B9E5CDBEBD7D27B85D419BA6AF12AEA0F835BBCEFC2C168CA",
     "InstanceProvider.get(AccountInterface::class.java)"},
    {"G618CD81FFF25B820E809D06BFDE8D3DE6586E113B235EB03F5019E6CF7F6C6C56082D913A535B9",
     "home using CompileTime JsonDeserializer"},
};

struct HdState {
  jmethodID intern_mid = nullptr;  // GetMethodID 一次缓存全循环（真机形态）
  int get_mid_calls = 0;
  std::map<std::string, jobject> cache;     // 本 walk 内容缓存（global ref = canonical plain）
  long calls = 0;
  long cache_hits = 0;
  long full_paths = 0;
  long mismatches = 0;  // golden trace 偏差（enc/plain 对照）
  long bad_same = 0;    // IsSameObject(in, ret) 失败（内容首次语义）
  long bad_canon = 0;   // 返回非 canonical plain（陈旧/错对象）
};
HdState g_hd;
// 跨 walk 持久：内容 -> canonical plain（global ref）。intern 表全局共享，
// 重复内容（跨 walk 也算重复）intern 返回既有 canonical（Android 语义），
// 每笔返回必须是它——陈旧句柄/错对象在两形态（自比/对照 canonical）下都检出。
std::map<std::string, jobject> g_canon_ever;

const GoldenPair* FindGolden(const std::string& enc) {
  for (const GoldenPair& p : kGoldenPairs) {
    if (enc == p.enc) return &p;
  }
  return nullptr;
}

// H.d 形态：GetStringUTFChars -> Release -> NewStringUTF -> CallObjectMethodV(intern)
// 重复 token：内容缓存命中快路径（解密跳过；返回句柄仍新 slot）。
jstring JNICALL n_nativeHdDecrypt(JNIEnv* env, jclass, jstring enc) {
  g_hd.calls++;
  // 四步 1：GetStringUTFChars(enc)（快路径也需要 enc 作缓存键，先读后判）
  const char* utf = env->functions->GetStringUTFChars(env, enc, nullptr);
  if (utf == nullptr) {
    g_hd.mismatches++;
    return nullptr;
  }
  std::string key(utf);
  env->functions->ReleaseStringUTFChars(env, enc, utf);  // 四步 2：Release

  const GoldenPair* golden = FindGolden(key);
  if (golden == nullptr) {
    g_hd.mismatches++;  // 未知 token（golden 对照失败）
    return nullptr;
  }

  // 缓存命中快路径：跳过 NewStringUTF/intern 解密四步尾部，返回新 local ref
  auto hit = g_hd.cache.find(key);
  if (hit != g_hd.cache.end()) {
    g_hd.cache_hits++;
    jstring r = static_cast<jstring>(env->functions->NewLocalRef(env, hit->second));
    if (r == nullptr) return nullptr;
    if (!NoteRetSlot(r)) return nullptr;
    // 每笔返回必须是该 token 的 canonical plain（陈旧/错对象检出）
    if (!env->functions->IsSameObject(env, r, g_canon_ever[key])) g_hd.bad_canon++;
    return r;
  }

  // 解密四步尾部：NewStringUTF(plain) -> CallObjectMethodV(plain.intern())
  g_hd.full_paths++;
  jstring in = env->functions->NewStringUTF(env, golden->plain);  // 四步 3
  if (in == nullptr) return nullptr;
  if (g_hd.intern_mid == nullptr) {
    jclass strCls = env->functions->FindClass(env, "java.lang.String");
    g_hd.intern_mid =
        env->functions->GetMethodID(env, strCls, "intern", "()Ljava/lang/String;");
    g_hd.get_mid_calls++;
    if (g_hd.intern_mid == nullptr) return nullptr;
  }
  jstring ret = static_cast<jstring>(CallObjectV(env, in, g_hd.intern_mid));  // 四步 4
  if (ret == nullptr) return nullptr;
  if (!NoteRetSlot(ret)) return nullptr;
  auto ever = g_canon_ever.find(key);
  if (ever == g_canon_ever.end()) {
    // 内容首次：intern 返回 self -> IsSameObject(in, ret) 必为 true（oracle 对照）
    if (!env->functions->IsSameObject(env, in, ret)) g_hd.bad_same++;
    g_canon_ever[key] = env->functions->NewGlobalRef(env, ret);
  } else {
    // 内容重复（intern 表已有）：intern 返回既有 canonical（Android 语义）
    if (!env->functions->IsSameObject(env, ret, ever->second)) g_hd.bad_canon++;
  }
  // golden trace 断言：四步回环后明文逐字节 == golden plain（内容偏差检出）
  {
    const char* back = env->functions->GetStringUTFChars(env, ret, nullptr);
    if (back == nullptr || std::strcmp(back, golden->plain) != 0) g_hd.mismatches++;
    if (back != nullptr) env->functions->ReleaseStringUTFChars(env, ret, back);
  }
  g_hd.cache[key] = env->functions->NewGlobalRef(env, ret);
  return ret;
}

// ---- 9) <clinit> 失败 erroneous：<clinit> 运行计数（只应为 1）----
long g_init_ticks = 0;
jlong JNICALL n_nativeInitTick(JNIEnv*, jclass) {
  return ++g_init_ticks;
}

// ===================== 工具 =====================

VMethod* FindMethod(VClass* klass, const char* name, bool is_static) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (int pass = 0; pass < 2; pass++) {
      auto& methods = pass == 0 ? k->direct_methods : k->virtual_methods;
      for (auto& m : methods) {
        if (m.name == name && m.IsStatic() == is_static) return &m;
      }
    }
  }
  return nullptr;
}

std::string PendingDesc(VMState& state) {
  if (!state.HasPendingException() || state.pending_exception == nullptr) return "";
  ObjHeader* ex = state.pending_exception;
  std::string desc = ex->klass != nullptr ? ex->klass->descriptor : "<bare>";
  for (VClass* c = ex->klass; c != nullptr; c = c->super_class) {
    for (const auto& f : c->instance_fields) {
      if (f.name == "detailMessage") {
        ObjHeader* msg = nullptr;
        std::memcpy(&msg, reinterpret_cast<VObject*>(ex)->field_data() + f.offset, sizeof(msg));
        if (msg != nullptr) desc += ": " + reinterpret_cast<VString*>(msg)->ToModifiedUtf8();
        return desc;
      }
    }
  }
  return desc;
}

void DummyTaskEntry(void*, void*) {}  // 预算探针伴跑任务（让 has_ready=true）

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("== jni bridge semantics (SecNeo shape) smoke ==\n");

  const char* fixture_path = argc > 1 ? argv[1] : "app/src/main/cpp/tests/out/jni_deep.dex";
  static std::vector<uint8_t> dex_bytes = ReadFile(fixture_path);
  if (dex_bytes.empty()) {
    std::printf("FAIL: cannot read fixture dex: %s\n", fixture_path);
    return 1;
  }
  std::string error;
  auto dex = dexvm::dex::DexFile::Open(dex_bytes.data(), dex_bytes.size(), &error);
  if (dex == nullptr) {
    std::printf("FAIL: DexFile::Open: %s\n", error.c_str());
    return 1;
  }

  dexvm::runtime::SetGuestSchedOps(dexvm::runtime::VmSchedBackendOps());

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  linker.AddAppDexFile(dex.get());

  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;
  g_state = &state;

  dexvm::jni::JavaVMExt vm;
  dexvm::jni::InitJavaVMExt(&vm);
  dexvm::jni::JNIEnvExt env;
  dexvm::jni::InitJNIEnvExt(&env, &vm, &state);
  JNIEnv* e = &env;
  g_env = e;

  jclass deepCls = e->functions->FindClass(e, "jdtest.DeepChain");
  CHECK(deepCls != nullptr, "FindClass jdtest.DeepChain");
  if (deepCls == nullptr) return 1;

  const JNINativeMethod kNatives[] = {
      {"nativeLoop", "(Ljava/lang/Object;I)J", reinterpret_cast<void*>(&n_nativeLoop)},
      {"nativePing", "(J)J", reinterpret_cast<void*>(&n_nativePing)},
      {"nativeIfaceLoop", "(Ljdtest/DeepChain$Cursor;I)J", reinterpret_cast<void*>(&n_nativeIfaceLoop)},
      {"nativeStrProbe", "(I)J", reinterpret_cast<void*>(&n_nativeStrProbe)},
      {"nativeWrap", "(J)J", reinterpret_cast<void*>(&n_nativeWrap)},
      {"nativeExhaust", "()J", reinterpret_cast<void*>(&n_nativeExhaust)},
      {"nativeStep", "(JLjava/lang/String;)Ljava/lang/String;",
       reinterpret_cast<void*>(&n_nativeStep)},
      {"nativeHdDecrypt", "(Ljava/lang/String;)Ljava/lang/String;",
       reinterpret_cast<void*>(&n_nativeHdDecrypt)},
      {"nativeInitTick", "()J", reinterpret_cast<void*>(&n_nativeInitTick)},
  };
  jint reg = e->functions->RegisterNatives(e, deepCls, kNatives, 9);
  CHECK(reg == 0, "RegisterNatives(9 SecNeo-shape callbacks)");

  VClass* deepKlass = reinterpret_cast<VClass*>(dexvm::jni::DecodeJObject(&env, deepCls));
  VMethod* loop_m = FindMethod(deepKlass, "loopProbe", true);
  VMethod* iface_m = FindMethod(deepKlass, "ifaceProbe", true);
  VMethod* str_m = FindMethod(deepKlass, "strProbe", true);
  VMethod* wrap_m = FindMethod(deepKlass, "wrapProbe", true);
  VMethod* exh_m = FindMethod(deepKlass, "exhaustProbe", true);
  VMethod* step_m = FindMethod(deepKlass, "step", false);
  VMethod* gd_m = FindMethod(deepKlass, "guestDeep", true);
  VMethod* nstep_m = FindMethod(deepKlass, "nativeStep", false);
  VMethod* mk_m = FindMethod(deepKlass, "makeTokenList", true);
  VMethod* walk_m = FindMethod(deepKlass, "tokenWalk", true);
  VMethod* crp_m = FindMethod(deepKlass, "clinitRetryProbe", true);
  CHECK(loop_m != nullptr && iface_m != nullptr && str_m != nullptr, "loop/iface/str probes");
  CHECK(wrap_m != nullptr && exh_m != nullptr && gd_m != nullptr, "wrap/exhaust/guestDeep");
  CHECK(step_m != nullptr && nstep_m != nullptr && nstep_m->jni_native_fn != nullptr,
        "nativeStep registered");
  CHECK(mk_m != nullptr && walk_m != nullptr && crp_m != nullptr, "tokenWalk/clinit probes");

  jclass nodeCls = e->functions->FindClass(e, "jdtest.DeepChain$Node");
  jclass curCls = e->functions->FindClass(e, "jdtest.DeepChain$Cursor");
  g_next_mid = e->functions->GetMethodID(e, nodeCls, "next", "()Ljdtest/DeepChain$Node;");
  g_cursor_next_mid = e->functions->GetMethodID(e, curCls, "next", "()Ljdtest/DeepChain$Cursor;");
  g_step_mid = e->functions->GetMethodID(e, deepCls, "step", "(JLjava/lang/String;)Ljava/lang/String;");
  CHECK(g_next_mid != nullptr && g_cursor_next_mid != nullptr && g_step_mid != nullptr,
        "Get*ID next/next/step");

  jobject self = e->functions->NewObject(
      e, deepCls, e->functions->GetMethodID(e, deepCls, "<init>", "()V"));
  CHECK(self != nullptr, "NewObject DeepChain");
  g_self = self;

  // 全部语义面在 main GuestTask fiber 上执行（栈口径受测面）
  bool ran = false;
  bool ok = dexvm::runtime::RunGuestMain(state, [&](VMState& st) -> bool {
    ran = true;
    Value rv{};

    // ============================================================
    // 1) 对象链终止循环（终止条件语义 + 每圈跨 JNI 边界栈漂移）
    // ============================================================
    std::printf("---- object-chain termination loop ----\n");
    for (int n : {5, 500, 5000}) {
      Value args[2]{};
      args[0].l = n;
      g_stack_drift = 0;
      bool lok = InvokeMethod(st, loop_m, args, 1, &rv);
      std::string diag = lok ? "" : PendingDesc(st);
      if (!lok) st.ClearException();
      long long count = lok ? rv.l : -1;
      char msg[192];
      std::snprintf(msg, sizeof(msg),
                    "loop n=%d: count=%lld (want %d) drift=%lld diag=%s", n, count, n,
                    g_stack_drift, diag.c_str());
      CHECK(lok && count == n && g_stack_drift < 256 * 1024, msg);
    }

    // ============================================================
    // 3) 接口派发终止循环
    // ============================================================
    std::printf("---- interface dispatch termination loop ----\n");
    {
      Value args[2]{};
      args[0].l = 500;
      g_stack_drift = 0;
      bool iok = InvokeMethod(st, iface_m, args, 1, &rv);
      std::string diag = iok ? "" : PendingDesc(st);
      if (!iok) st.ClearException();
      long long count = iok ? rv.l : -1;
      char msg[192];
      std::snprintf(msg, sizeof(msg), "iface loop n=500: count=%lld (want 500) drift=%lld diag=%s",
                    count, g_stack_drift, diag.c_str());
      CHECK(iok && count == 500 && g_stack_drift < 256 * 1024, msg);
    }

    // ============================================================
    // 2) 字符串语义回环（Android MUTF-8 逐字节）
    // ============================================================
    std::printf("---- string semantics round-trip ----\n");
    for (int i = 0; i < 5; i++) {
      Value args[1]{};
      args[0].l = i;
      bool sok = InvokeMethod(st, str_m, args, 1, &rv);
      std::string diag = sok ? "" : PendingDesc(st);
      if (!sok) st.ClearException();
      long long bad = sok ? rv.l : -1;
      char msg[160];
      std::snprintf(msg, sizeof(msg), "string case %d: mismatches=%lld diag=%s", i, bad,
                    diag.c_str());
      CHECK(sok && bad == 0, msg);
    }

    // ============================================================
    // 4) local ref 代回绕（纯语义：悬挂 jobject 误判）
    // ============================================================
    std::printf("---- local ref generation wraparound ----\n");
    for (jlong iters : {64LL, 128LL, 2048LL}) {
      Value args[1]{};
      args[0].l = iters;
      bool wok = InvokeMethod(st, wrap_m, args, 1, &rv);
      std::string diag = wok ? "" : PendingDesc(st);
      if (!wok) st.ClearException();
      long long misread = wok ? rv.l : -1;
      char msg[160];
      std::snprintf(msg, sizeof(msg), "wrap iters=%lld: stale read %lld (want 0) diag=%s", iters,
                    misread, diag.c_str());
      CHECK(wok && misread == 0, msg);
    }

    // ============================================================
    // 5) local ref 耗尽语义（null + OutOfMemoryError）
    // ============================================================
    std::printf("---- local ref exhaustion semantics ----\n");
    {
      bool xok = InvokeMethod(st, exh_m, nullptr, 0, &rv);
      std::string diag = xok ? "" : PendingDesc(st);
      if (!xok) st.ClearException();
      long long n = xok ? rv.l : -1;
      char msg[160];
      std::snprintf(msg, sizeof(msg), "exhaust: created %lld (want >0 = null + OOM) diag=%s", n,
                    diag.c_str());
      CHECK(xok && n > 0, msg);
    }

    // ============================================================
    // 6) 深递归护栏（配套缓解验证；非根因结论）
    // ============================================================
    std::printf("---- recursion guard (companion mitigation) ----\n");
    {
      Value sval{};
      sval.ref = linker.AllocString(u"secneo-payload");
      Value args[3]{};
      args[0].ref = dexvm::jni::DecodeJObject(&env, self);
      args[1].l = 100;
      args[2] = sval;
      g_cycles = 0;
      bool d1 = InvokeMethod(st, step_m, args, 3, &rv);
      std::string diag = d1 ? "" : PendingDesc(st);
      if (!d1) st.ClearException();
      char msg[192];
      std::snprintf(msg, sizeof(msg), "deep 100: ok=%d cycles=%lld diag=%s", d1 ? 1 : 0, g_cycles,
                    diag.c_str());
      CHECK(d1 && g_cycles >= 100, msg);

      // 25000 层：可接受停点 = 完成 / SOE / OOM（local ref 512 先于栈尽）；
      // 崩溃 = 根因未修
      args[1].l = 25000;
      g_cycles = 0;
      d1 = InvokeMethod(st, step_m, args, 3, &rv);
      diag = d1 ? "" : PendingDesc(st);
      bool clean = d1 || diag.find("StackOverflowError") != std::string::npos ||
                   diag.find("OutOfMemoryError") != std::string::npos;
      if (!d1) st.ClearException();
      std::snprintf(msg, sizeof(msg), "deep 25000 mixed: ok=%d cycles=%lld diag=%s", d1 ? 1 : 0,
                    g_cycles, diag.c_str());
      CHECK(clean && g_cycles >= 100, msg);

      // 纯 guest 递归：栈尽须 SOE（护栏），不崩
      Value gargs[1]{};
      gargs[0].l = 25000;
      d1 = InvokeMethod(st, gd_m, gargs, 1, &rv);
      diag = d1 ? "" : PendingDesc(st);
      bool soe = diag.find("StackOverflowError") != std::string::npos;
      if (!d1) st.ClearException();
      std::snprintf(msg, sizeof(msg), "guestDeep 25000: ok=%d soe=%d diag=%s", d1 ? 1 : 0,
                    soe ? 1 : 0, diag.c_str());
      CHECK(d1 || soe, msg);
    }

    // ============================================================
    // 8) 真形态 token 清单遍历（H.d 四步 + 缓存快路径 + golden trace）
    // ============================================================
    std::printf("---- real-form token walk (H.d shape + golden trace) ----\n");
    for (int n : {5, 500, 5000}) {
      g_hd = HdState{};
      g_last_ret_slot = 0;
      Value margs[1]{};
      margs[0].l = n;
      Value arr{};
      bool mok = InvokeMethod(st, mk_m, margs, 1, &arr);
      Value wargs[1]{};
      wargs[0] = arr;
      bool wok = InvokeMethod(st, walk_m, wargs, 1, &rv);
      std::string diag = wok ? "" : PendingDesc(st);
      if (!wok) st.ClearException();
      long long processed = wok ? rv.l : -1;
      int distinct = n < 10 ? n : 10;
      char msg[288];
      std::snprintf(msg, sizeof(msg),
                    "walk n=%d: processed=%lld (want %d) calls=%ld full=%ld (want %d) "
                    "hits=%ld golden_mismatch=%ld bad_same=%ld bad_canon=%ld diag=%s",
                    n, processed, n, g_hd.calls, g_hd.full_paths, distinct, g_hd.cache_hits,
                    g_hd.mismatches, g_hd.bad_same, g_hd.bad_canon, diag.c_str());
      CHECK(mok && wok && processed == n && g_hd.calls == n && g_hd.full_paths == distinct &&
                g_hd.cache_hits == n - distinct && g_hd.mismatches == 0 && g_hd.bad_same == 0 &&
                g_hd.bad_canon == 0,
            msg);
    }
    {
      char msg[160];
      std::snprintf(msg, sizeof(msg), "return handles fresh-slot monotonic: %s",
                    g_slot_mono_ok ? "yes" : "NO (stale slot reused)");
      CHECK(g_slot_mono_ok, msg);
    }

    // ============================================================
    // 9) <clinit> 失败 erroneous（Android：不再重跑）
    // ============================================================
    std::printf("---- clinit failure erroneous semantics ----\n");
    {
      g_init_ticks = 0;
      Value targs[1]{};
      targs[0].l = 3;
      bool cok = InvokeMethod(st, crp_m, targs, 1, &rv);
      std::string diag = cok ? "" : PendingDesc(st);
      if (!cok) st.ClearException();
      long long failures = cok ? rv.l : -1;
      char msg[192];
      std::snprintf(msg, sizeof(msg),
                    "clinitRetryProbe x3: failures=%lld (want 3) clinit_runs=%ld (want 1) diag=%s",
                    failures, g_init_ticks, diag.c_str());
      CHECK(cok && failures == 3 && g_init_ticks == 1, msg);
    }
    return true;
  });
  CHECK(ran && ok, "semantic scenarios ran on main GuestTask");

  // ============================================================
  // 7) 调度器预算中止归因（非信号失败不得报 "unknown signal"）
  // ============================================================
  std::printf("---- scheduler budget abort attribution ----\n");
  {
    using dexvm::runtime::GuestRunFailureReason;
    using dexvm::runtime::GuestRuntimeReset;
    using dexvm::runtime::GuestSched;
    using dexvm::runtime::SetGuestRunBudgetMs;
    const dexvm::runtime::GuestSchedOps* ops = GuestSched();
    SetGuestRunBudgetMs(1);
    if (ops != nullptr && ops->spawn != nullptr) {
      ops->spawn(&DummyTaskEntry, nullptr, 0);
    }
    bool bok = dexvm::runtime::RunGuestMain(state, [&](VMState&) -> bool {
      for (int i = 0; i < 10000000; i++) {
        if (ops == nullptr || !ops->yield_now()) {
          if (i > 0) break;
        }
      }
      return true;
    });
    const char* reason = GuestRunFailureReason();
    std::string rs = reason != nullptr ? reason : "";
    char msg[224];
    std::snprintf(msg, sizeof(msg), "budget abort: ok=%d reason=%s", bok ? 1 : 0, rs.c_str());
    CHECK(!bok && rs.find("budget") != std::string::npos, msg);
    GuestRuntimeReset(&state);
    SetGuestRunBudgetMs(60000);
  }

  std::printf("== jni bridge semantics smoke: %s (%d failures) ==\n",
              g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
