// 字体桥：guest Typeface/FontFamily/Paint 度量 -> 宿主真 Android 字体栈。
//
// AOSP 口径：字体解析、fallback 链与文本 shaping/measure 都在平台 native
// （Minikin/HarfBuzz），VM 不重复实现。实现分两段：
//   - guest fiber 侧：intrinsic 只把请求放入 native 队列并等 condvar 结果；
//   - 宿主 dexvm-font 线程（com.dexvm.host.FontBridge.workerLoop）：
//     执行真 Typeface/Paint（Java->native 方向，ART 安全）。
// 为什么不能直接调：解释器跑在自研 fiber 栈，ART 用 Java 线程栈边界做 JNI
// 栈溢出判定，从 fiber 直接 JNI 会 pending StackOverflowError 后 abort。
#include "core/builtins_internal.h"
#include "core/font_worker.h"
#include "core/framework_builtins.h"
#include "core/framework_builtins_internal.h"
#include "core/host_java.h"
#include "core/intrinsic.h"
#include "core/vm_types.h"

#include <sys/stat.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace dexvm::runtime {

namespace {

void* g_host_vm = nullptr;

struct HostFontRequest {
  uint64_t id = 0;
  int op = 0;
  int style = 0;
  float size = 0.0f;
  int64_t base = 0;
  std::string str;
  std::u16string text;
  bool done = false;
  bool metrics_ok = false;
  int64_t handle = 0;
  float width = -1.0f;
  int32_t metrics[5] = {0, 0, 0, 0, 0};
};

std::mutex g_font_mu;
std::condition_variable g_font_cv;
std::deque<std::shared_ptr<HostFontRequest>> g_font_pending;
std::unordered_map<uint64_t, std::shared_ptr<HostFontRequest>> g_font_inflight;
uint64_t g_font_next_id = 1;
bool g_font_worker_alive = false;

std::string ContextApkPath() {
  if (g_context_resources == nullptr) return "";
  VField* f = FindInstanceField(g_context_resources->klass, "mApkPath");
  if (f == nullptr) return "";
  ObjHeader* s = GetRefField(g_context_resources, f->offset);
  return s != nullptr ? static_cast<VString*>(s)->ToModifiedUtf8() : std::string();
}

bool WriteFontFile(const std::string& name, const std::vector<uint8_t>& bytes,
                   std::string* out_path) {
  if (g_ctx_data_dir.empty()) return false;
  const std::string dir = g_ctx_data_dir + "/fonts";
  ::mkdir(dir.c_str(), 0700);
  const std::string path = dir + "/" + name;
  FILE* fp = std::fopen(path.c_str(), "wb");
  if (fp == nullptr) return false;
  size_t written = std::fwrite(bytes.data(), 1, bytes.size(), fp);
  std::fclose(fp);
  if (written != bytes.size()) return false;
  if (out_path != nullptr) *out_path = path;
  return true;
}

}  // namespace

void SetHostJavaVM(void* vm) { g_host_vm = vm; }
void* HostJavaVM() { return g_host_vm; }

bool RequestHostFont(int op, int style, float size, int64_t base, const std::string& str,
                     const std::u16string& text, int64_t* out_handle, float* out_width,
                     int32_t out_metrics[5], bool* out_metrics_ok, int timeout_ms) {
  if (HostJavaVM() == nullptr) return false;  // 桌面单测/无宿主：回落网格
#if defined(__ANDROID__)
  static std::atomic<int> req_logs{0};
  if (req_logs.fetch_add(1, std::memory_order_relaxed) < 5) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmFont", "request op=%d alive=%d", op,
                        HostFontWorkerAlive() ? 1 : 0);
  }
#endif
  std::shared_ptr<HostFontRequest> req;
  {
    std::lock_guard<std::mutex> lk(g_font_mu);
    req = std::make_shared<HostFontRequest>();
    req->id = g_font_next_id++;
    req->op = op;
    req->style = style;
    req->size = size;
    req->base = base;
    req->str = str;
    req->text = text;
    g_font_pending.push_back(req);
  }
  g_font_cv.notify_all();
  std::unique_lock<std::mutex> lk(g_font_mu);
  if (!g_font_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                          [&] { return req->done; })) {
    return false;  // worker 未启动/超时：调用方回落网格度量
  }
  if (out_handle != nullptr) *out_handle = req->handle;
  if (out_width != nullptr) *out_width = req->width;
  if (out_metrics != nullptr && out_metrics_ok != nullptr) {
    *out_metrics_ok = req->metrics_ok;
    for (int i = 0; i < 5; i++) out_metrics[i] = req->metrics[i];
  }
  return true;
}

uint64_t HostFontNextRequest() {
  std::unique_lock<std::mutex> lk(g_font_mu);
  g_font_worker_alive = true;
  g_font_cv.wait(lk, [] { return !g_font_pending.empty(); });
  std::shared_ptr<HostFontRequest> req = g_font_pending.front();
  g_font_pending.pop_front();
  g_font_inflight[req->id] = req;
  return req->id;
}

int HostFontOp(uint64_t id) {
  std::lock_guard<std::mutex> lk(g_font_mu);
  auto it = g_font_inflight.find(id);
  return it != g_font_inflight.end() ? it->second->op : 0;
}

int HostFontStyle(uint64_t id) {
  std::lock_guard<std::mutex> lk(g_font_mu);
  auto it = g_font_inflight.find(id);
  return it != g_font_inflight.end() ? it->second->style : 0;
}

float HostFontSize(uint64_t id) {
  std::lock_guard<std::mutex> lk(g_font_mu);
  auto it = g_font_inflight.find(id);
  return it != g_font_inflight.end() ? it->second->size : 0.0f;
}

int64_t HostFontBase(uint64_t id) {
  std::lock_guard<std::mutex> lk(g_font_mu);
  auto it = g_font_inflight.find(id);
  return it != g_font_inflight.end() ? it->second->base : 0;
}

std::string HostFontString(uint64_t id) {
  std::lock_guard<std::mutex> lk(g_font_mu);
  auto it = g_font_inflight.find(id);
  return it != g_font_inflight.end() ? it->second->str : std::string();
}

std::u16string HostFontText(uint64_t id) {
  std::lock_guard<std::mutex> lk(g_font_mu);
  auto it = g_font_inflight.find(id);
  return it != g_font_inflight.end() ? it->second->text : std::u16string();
}

void HostFontComplete(uint64_t id, int64_t handle, float width, const int32_t* metrics,
                      bool metrics_ok) {
  std::lock_guard<std::mutex> lk(g_font_mu);
  auto it = g_font_inflight.find(id);
  if (it == g_font_inflight.end()) return;
  std::shared_ptr<HostFontRequest> req = it->second;
  req->handle = handle;
  req->width = width;
  req->metrics_ok = metrics_ok && metrics != nullptr;
  if (req->metrics_ok) {
    for (int i = 0; i < 5; i++) req->metrics[i] = metrics[i];
  }
  req->done = true;
  g_font_inflight.erase(it);
  g_font_cv.notify_all();
}

bool HostFontWorkerAlive() {
  std::lock_guard<std::mutex> lk(g_font_mu);
  return g_font_worker_alive;
}

// render 管线（TextView.onMeasure）用：宿主真字体度量（默认字体，handle=0）。
bool HostMeasureTextUtf16(const uint16_t* chars, int32_t len, float text_size,
                          float* out_width) {
  if (out_width == nullptr) return false;
  if (chars == nullptr || len <= 0) {
    *out_width = 0.0f;
    return true;
  }
  std::u16string text(reinterpret_cast<const char16_t*>(chars), static_cast<size_t>(len));
  float w = -1.0f;
  if (!RequestHostFont(kFontOpMeasureText, 0, text_size, 0, std::string(), text, nullptr, &w,
                       nullptr, nullptr)) {
    return false;
  }
  if (w < 0.0f) return false;
  *out_width = w;
  return true;
}

bool HostFontMetricsInt(float text_size, int32_t out[5]) {
  if (out == nullptr) return false;
  int32_t metrics[5] = {0, 0, 0, 0, 0};
  bool ok = false;
  if (!RequestHostFont(kFontOpFontMetrics, 0, text_size, 0, std::string(), std::u16string(),
                       nullptr, nullptr, metrics, &ok)) {
    return false;
  }
  if (!ok) return false;
  for (int i = 0; i < 5; i++) out[i] = metrics[i];
  return true;
}

Value VmFontsAvailable(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = HostJavaVM() != nullptr ? 1 : 0;
  return r;
}

Value VmFontsCreateFromFamily(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2) return r;
  std::string family;
  if (args[0].ref != nullptr) {
    family = static_cast<VString*>(args[0].ref)->ToModifiedUtf8();
  }
  int64_t handle = 0;
  RequestHostFont(kFontOpCreateFamily, args[1].i, 0.0f, 0, family, std::u16string(), &handle,
                  nullptr, nullptr, nullptr);
  r.l = handle;
  return r;
}

Value VmFontsCreateFromFile(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) return r;
  std::string path = static_cast<VString*>(args[0].ref)->ToModifiedUtf8();
  int64_t handle = 0;
  RequestHostFont(kFontOpCreateFile, args[1].i, 0.0f, 0, path, std::u16string(), &handle,
                  nullptr, nullptr, nullptr);
  r.l = handle;
  return r;
}

Value VmFontsRestyle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2) return r;
  int64_t handle = 0;
  RequestHostFont(kFontOpRestyle, args[1].i, 0.0f, args[0].l, std::string(), std::u16string(),
                  &handle, nullptr, nullptr, nullptr);
  r.l = handle;
  return r;
}

Value VmFontsCreateFromResource(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2) return r;
  const uint32_t res_id = static_cast<uint32_t>(args[0].i);
  const int32_t style = args[1].i;
  const ResourceSeam* seam = GetResourceSeam();
  const std::string apk = ContextApkPath();
  if (seam == nullptr || seam->open_resource_file == nullptr || apk.empty()) return r;
  std::string entry;
  std::vector<uint8_t> bytes;
  std::string err;
  if (!seam->open_resource_file(apk, res_id, &entry, &bytes, &err) || bytes.empty()) {
    return r;
  }
  std::string path;
  const std::string name =
      "res_" + std::to_string(res_id) + "_" + std::to_string(style) + ".font";
  if (!WriteFontFile(name, bytes, &path)) return r;
  int64_t handle = 0;
  RequestHostFont(kFontOpCreateFile, style, 0.0f, 0, path, std::u16string(), &handle, nullptr,
                  nullptr, nullptr);
  r.l = handle;
  (void)state;
  return r;
}

Value VmFontsMeasureText(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  r.f = -1.0f;
  if (n < 3 || args[1].ref == nullptr) {
    r.f = 0.0f;
    return r;
  }
  VString* s = static_cast<VString*>(args[1].ref);
  std::u16string text(reinterpret_cast<const char16_t*>(s->chars()),
                      static_cast<size_t>(s->length));
  float width = -1.0f;
  if (RequestHostFont(kFontOpMeasureText, 0, args[2].f, args[0].l, std::string(), text,
                      nullptr, &width, nullptr, nullptr)) {
    r.f = width;
  }
  return r;
}

Value VmFontsFontMetrics(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  if (n < 3 || args[2].ref == nullptr) return r;
  VArray* arr = reinterpret_cast<VArray*>(args[2].ref);
  if (arr->length < 5) return r;
  int32_t metrics[5] = {0, 0, 0, 0, 0};
  bool good = false;
  if (!RequestHostFont(kFontOpFontMetrics, 0, args[1].f, args[0].l, std::string(),
                       std::u16string(), nullptr, nullptr, metrics, &good) ||
      !good) {
    return r;
  }
  for (int i = 0; i < 5; i++) {
    int32_t v = metrics[i];
    std::memcpy(arr->data() + static_cast<size_t>(i) * 8, &v, sizeof(v));
  }
  r.i = 1;
  return r;
}

}  // namespace dexvm::runtime
