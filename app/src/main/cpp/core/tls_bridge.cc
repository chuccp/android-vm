// Host TLS bridge：请求队列 + 协程化回复等待 + guest 薄壳 intrinsic。
//
// 设计要点见 core/tls_bridge.h。本 TU 不依赖 JNI/OpenSSL：
//   - Android 进程：jni/runtime_contract.cpp 的 nativeTlsTake/nativeTlsReply
//     把宿主 TlsBridge.java 线程接到 TlsTakeRequest/TlsDeliverReply；
//   - 宿主探针：服务线程直接使用 TlsTakeRequest/TlsDeliverReply（OpenSSL 在
//     探针 TU 内，core 不引入 TLS 库依赖）。
#include "core/tls_bridge.h"

#include "core/boot_network.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "core/builtins.h"
#include "core/builtins_internal.h"
#include "core/class_linker.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define TLS_LOG(...) __android_log_print(ANDROID_LOG_INFO, "DexVmTlsBridge", __VA_ARGS__)
#else
#define TLS_LOG(...) \
  do {               \
  } while (0)
#endif

namespace dexvm::runtime {
namespace {

constexpr int kReplyPollSliceMs = 10;  // 协作等待切片（丢 wake 时兜底延迟）

struct TlsState {
  std::mutex mu;
  std::condition_variable req_cv;
  std::deque<TlsRequest> requests;
  std::map<int64_t, TlsReply> replies;
  // 请求 id -> 等待中的 guest task（回复线程用它跨线程 wake）
  std::map<int64_t, void*> wait_tasks;
  int64_t next_id = 0;
  bool bridge_available = false;
  bool shutdown = false;
};

TlsState& State() {
  static TlsState state;
  return state;
}

long long NowMs() {
  const GuestSchedOps* ops = GuestSched();
  if (ops != nullptr && ops->now_ms != nullptr) return ops->now_ms();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool ReplyReady(void* arg) {
  const int64_t id = *static_cast<const int64_t*>(arg);
  TlsState& s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  return s.replies.find(id) != s.replies.end();
}

void* CurrentGuestTask() {
  const GuestSchedOps* ops = GuestSched();
  if (ops == nullptr || ops->current_task == nullptr) return nullptr;
  return ops->current_task();
}

int64_t Enqueue(TlsRequest req) {
  TlsState& s = State();
  void* task = CurrentGuestTask();
  std::lock_guard<std::mutex> lock(s.mu);
  int64_t id = ++s.next_id;
  req.id = id;
  s.requests.push_back(std::move(req));
  s.wait_tasks[id] = task;
  s.req_cv.notify_all();
  return id;
}

// UTF-8 -> UTF-16（宿主元数据/错误串；异常路径替换非法字节，不抛）。
std::u16string Utf8ToUtf16(const std::string& bytes) {
  std::u16string out;
  out.reserve(bytes.size());
  size_t i = 0;
  while (i < bytes.size()) {
    unsigned char c = static_cast<unsigned char>(bytes[i]);
    uint32_t cp = 0xFFFD;
    size_t n = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < bytes.size()) {
      cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(bytes[i + 1]) & 0x3F);
      n = 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < bytes.size()) {
      cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(bytes[i + 1]) & 0x3F) << 6) |
           (static_cast<unsigned char>(bytes[i + 2]) & 0x3F);
      n = 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < bytes.size()) {
      cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(bytes[i + 1]) & 0x3F) << 12) |
           ((static_cast<unsigned char>(bytes[i + 2]) & 0x3F) << 6) |
           (static_cast<unsigned char>(bytes[i + 3]) & 0x3F);
      n = 4;
    }
    i += n;
    if (cp <= 0xFFFF) {
      out.push_back(static_cast<char16_t>(cp));
    } else {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    }
  }
  return out;
}

Value MakeByteArrayValue(VMState& state, const std::string& bytes, bool* ok) {
  VClass* arr_class =
      state.linker != nullptr
          ? state.linker->FindArrayClass(state.linker->PrimitiveClass('B'))
          : nullptr;
  VArray* arr = arr_class != nullptr
                    ? state.linker->AllocArray(arr_class, static_cast<int32_t>(bytes.size()))
                    : nullptr;
  if (arr == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "TLS read buffer");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < bytes.size(); i++) {
    uint64_t slot = static_cast<uint8_t>(bytes[i]);
    std::memcpy(arr->data() + i * 8, &slot, sizeof(slot));
  }
  Value r{};
  r.ref = arr;
  return r;
}

std::string ReadGuestBytes(VArray* arr, int32_t off, int32_t len) {
  std::string out;
  if (arr == nullptr || off < 0 || len <= 0 || off > arr->length - len) return out;
  out.resize(static_cast<size_t>(len));
  for (int32_t i = 0; i < len; i++) {
    uint64_t slot = 0;
    std::memcpy(&slot, arr->data() + (static_cast<size_t>(off) + i) * 8, sizeof(slot));
    out[static_cast<size_t>(i)] = static_cast<char>(slot & 0xFF);
  }
  return out;
}

// 宿主回复状态 -> guest 异常（描述符均在 runtime.dex）。
void ThrowTlsReply(VMState& state, const TlsReply& reply) {
  std::string message = reply.data;
  if (message.empty()) message = "TLS bridge request failed (status=" +
                                  std::to_string(reply.status) + ")";
  const char* descriptor = "Ljava/io/IOException;";
  switch (reply.status) {
    case kTlsTimeout:
      descriptor = "Ljava/net/SocketTimeoutException;";
      break;
    case kTlsSslError:
      descriptor = "Ljavax/net/ssl/SSLException;";
      break;
    case kTlsConnectError:
      descriptor = "Ljava/net/ConnectException;";
      break;
    case kTlsClosed:
    case kTlsIoError:
    case kTlsUnavailable:
    default:
      descriptor = "Ljava/io/IOException;";
      break;
  }
  state.ThrowNew(descriptor, message);
}

// 统一收尾：等待回复并把非 OK 状态映射为 guest 异常。
bool AwaitOk(VMState& state, int64_t id, int wait_ms, TlsReply* reply, bool* ok) {
  if (!TlsAwaitReply(id, wait_ms, reply)) {
    TLS_LOG("request %lld reply timeout (%d ms)", static_cast<long long>(id), wait_ms);
    state.ThrowNew("Ljava/net/SocketTimeoutException;",
                   "TLS bridge reply timed out (host TLS thread unresponsive)");
    *ok = false;
    return false;
  }
  if (reply->status != kTlsOk) {
    TLS_LOG("request %lld failed status=%d: %s", static_cast<long long>(id), reply->status,
            reply->data.c_str());
    ThrowTlsReply(state, *reply);
    *ok = false;
    return false;
  }
  return true;
}

int WaitBudget(int host_timeout_ms, int fallback_ms) {
  if (host_timeout_ms <= 0) return fallback_ms;
  long long budget = static_cast<long long>(host_timeout_ms) + 5000;
  return static_cast<int>(std::min<long long>(budget, 10 * 60 * 1000));
}

}  // namespace

// ---------------------------------------------------------------------------
// 队列 / 回复面
// ---------------------------------------------------------------------------

void TlsSetBridgeAvailable(bool available) {
  TlsState& s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  s.bridge_available = available;
}

bool TlsBridgeAvailable() {
  TlsState& s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  return s.bridge_available;
}

int64_t TlsQueueOpen(const std::string& host, int port, int connect_timeout_ms) {
  TlsRequest req;
  req.kind = kTlsOpOpen;
  req.host = host;
  req.port = port;
  req.timeout_ms = connect_timeout_ms;
  return Enqueue(std::move(req));
}

int64_t TlsQueueWrite(int64_t handle, const std::string& data) {
  TlsRequest req;
  req.kind = kTlsOpWrite;
  req.handle = handle;
  req.data = data;
  return Enqueue(std::move(req));
}

int64_t TlsQueueRead(int64_t handle, int max_bytes, int so_timeout_ms) {
  TlsRequest req;
  req.kind = kTlsOpRead;
  req.handle = handle;
  req.max_bytes = max_bytes;
  req.timeout_ms = so_timeout_ms;
  return Enqueue(std::move(req));
}

int64_t TlsQueueClose(int64_t handle) {
  TlsRequest req;
  req.kind = kTlsOpClose;
  req.handle = handle;
  return Enqueue(std::move(req));
}

bool TlsAwaitReply(int64_t id, int timeout_ms, TlsReply* reply) {
  TlsState& s = State();
  const long long deadline = timeout_ms > 0 ? NowMs() + timeout_ms : -1;
  const GuestSchedOps* ops = GuestSched();
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(s.mu);
      auto it = s.replies.find(id);
      if (it != s.replies.end()) {
        if (reply != nullptr) *reply = std::move(it->second);
        s.replies.erase(it);
        s.wait_tasks.erase(id);
        return true;
      }
    }
    long long now = NowMs();
    if (deadline >= 0 && now >= deadline) {
      std::lock_guard<std::mutex> lock(s.mu);
      s.replies.erase(id);
      s.wait_tasks.erase(id);
      return false;
    }
    long long until = deadline >= 0 ? std::min(deadline, now + kReplyPollSliceMs)
                                    : now + kReplyPollSliceMs;
    if (ops != nullptr && ops->wait != nullptr && ops->current_task != nullptr &&
        ops->current_task() != nullptr) {
      // 谓词只读 TlsState（调度器锁内调用，不回调调度器接口）。
      int64_t wait_id = id;
      ops->wait(kGuestWaitJoin, until, &ReplyReady, &wait_id);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}

bool TlsTakeRequest(TlsRequest* out) {
  TlsState& s = State();
  std::unique_lock<std::mutex> lock(s.mu);
  s.req_cv.wait(lock, [&s] { return s.shutdown || !s.requests.empty(); });
  if (s.requests.empty()) return false;  // shutdown
  if (out != nullptr) *out = std::move(s.requests.front());
  s.requests.pop_front();
  return true;
}

void TlsDeliverReply(int64_t id, int32_t status, int64_t handle, const std::string& data) {
  TlsState& s = State();
  void* task = nullptr;
  {
    std::lock_guard<std::mutex> lock(s.mu);
    s.replies[id] = TlsReply{status, handle, data};
    auto it = s.wait_tasks.find(id);
    if (it != s.wait_tasks.end()) {
      task = it->second;
      s.wait_tasks.erase(it);
    }
  }
  // 注意：wake 在 TlsState 锁外调用（避免 scheduler.mu <-> tls.mu 反序）。
  if (task != nullptr) {
    const GuestSchedOps* ops = GuestSched();
    if (ops != nullptr && ops->wake != nullptr) ops->wake(task, kGuestWaitJoin);
  }
}

std::string TlsEncodeRequest(const TlsRequest& req) {
  auto put_u32 = [](std::string* out, uint32_t v) {
    out->push_back(static_cast<char>(v & 0xFF));
    out->push_back(static_cast<char>((v >> 8) & 0xFF));
    out->push_back(static_cast<char>((v >> 16) & 0xFF));
    out->push_back(static_cast<char>((v >> 24) & 0xFF));
  };
  auto put_i64 = [](std::string* out, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; i++) out->push_back(static_cast<char>((u >> (8 * i)) & 0xFF));
  };
  std::string out;
  out.reserve(1 + 8 + 8 + 4 + 4 + 4 + 4 + 4 + req.host.size() + req.data.size());
  out.push_back(static_cast<char>(req.kind));
  put_i64(&out, req.id);
  put_i64(&out, req.handle);
  put_u32(&out, static_cast<uint32_t>(req.port));
  put_u32(&out, static_cast<uint32_t>(req.timeout_ms));
  put_u32(&out, static_cast<uint32_t>(req.max_bytes));
  put_u32(&out, static_cast<uint32_t>(req.host.size()));
  put_u32(&out, static_cast<uint32_t>(req.data.size()));
  out.append(req.host);
  out.append(req.data);
  return out;
}

void TlsResetWaiters() {
  TlsState& s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  s.wait_tasks.clear();
  s.replies.clear();
  s.requests.clear();
}

void TlsShutdown() {
  TlsState& s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  s.shutdown = true;
  s.req_cv.notify_all();
}

void TlsResume() {
  TlsState& s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  s.shutdown = false;
}

// ---------------------------------------------------------------------------
// guest 薄壳 intrinsic（javax.net.ssl.BridgedTls，BindWellKnownNatives 绑定）
// ---------------------------------------------------------------------------

Value TlsNativeOpen(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  if (BootNetworkOffline()) {
    // 首帧前离线启动：TLS 请求立即失败，app 走本地配置/骨架。
    state.ThrowNew("Ljava/io/IOException;", "network offline during boot");
    *ok = false;
    return Value{};
  }
  if (n < 3 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "TLS host");
    *ok = false;
    return Value{};
  }
  const std::string host = Mutf8Arg(args[0]);
  const int port = args[1].i;
  const int connect_timeout_ms = args[2].i;
  TLS_LOG("guest open %s:%d timeout=%d", host.c_str(), port, connect_timeout_ms);
  if (!TlsBridgeAvailable()) {
    TLS_LOG("guest open %s:%d UNAVAILABLE (host bridge not started)", host.c_str(), port);
    state.ThrowNew("Ljava/io/IOException;",
                   "TLS bridge unavailable: host TlsBridge thread is not running");
    *ok = false;
    return Value{};
  }
  const int64_t id = TlsQueueOpen(host, port, connect_timeout_ms);
  TlsReply reply;
  if (!AwaitOk(state, id, WaitBudget(connect_timeout_ms, 60000), &reply, ok)) {
    TLS_LOG("guest open %s:%d reply timeout", host.c_str(), port);
    return Value{};
  }
  if (reply.handle == 0) {
    TLS_LOG("guest open %s:%d invalid handle", host.c_str(), port);
    state.ThrowNew("Ljava/io/IOException;", "TLS bridge open returned an invalid handle");
    *ok = false;
    return Value{};
  }
  TLS_LOG("guest open %s:%d OK handle=%lld", host.c_str(), port,
          static_cast<long long>(reply.handle));
  // 元数据自描述：handle 行由 native 前置（宿主不参与编号）。
  std::string metadata = "handle=" + std::to_string(reply.handle) + "\n" + reply.data;
  return MakeStr(state, Utf8ToUtf16(metadata), ok);
}

#if defined(__ANDROID__)
// TEMP BRIDGE（feed 解锁工作项；正式修复后移除）：
// 加固壳（SecNeo）使 NetworkMonitorDecorator 构造器在本 VM 下拿到四个空串
// （壳 native 以 NewStringUTF("") 造键 → H.d("")=""），java8 Set.of 判重抛
// IllegalArgumentException → 模块 ServiceManifest 的 BuilderDecorator 实例化
// 循环整体中止（b1.h catch Exception 返回 null；u0.a=false 不走 ServiceLoader
// 兜底）→ GlobalRequestDecorator 的全局头拦截器缺席 → appcloud2 拒绝请求
// （"za 消息头不能为空"）→ 底栏动态配置失败、feed 无法初始化。
// 过渡方案：按 guest 自己的 app/n0/a.b()（AppBuildInfo）结果补 x-app-za。
namespace {

bool ContainsIgnoreCase(const std::string& hay, const char* needle) {
  const size_t n = std::strlen(needle);
  if (hay.size() < n) return false;
  for (size_t i = 0; i + n <= hay.size(); i++) {
    bool match = true;
    for (size_t k = 0; k < n; k++) {
      if (::tolower(static_cast<unsigned char>(hay[i + k])) !=
          ::tolower(static_cast<unsigned char>(needle[k]))) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

// guest 侧 AppBuildInfo（x-app-za 真实值）一次性求值；失败返回空。
std::string ComputeZaHeader(VMState& state) {
  static std::string cached;
  if (!cached.empty()) return cached;
  if (state.linker == nullptr) return {};
  VClass* k = state.linker->FindClass("Lcom/zhihu/android/app/n0/a;");
  VMethod* bm = nullptr;
  if (k != nullptr) {
    for (auto& m : k->direct_methods) {
      if (m.name == "b" && m.return_type == "Ljava/lang/String;" && m.param_types.empty()) {
        bm = &m;
        break;
      }
    }
  }
  if (bm == nullptr) return {};
  ObjHeader* saved = state.pending_exception;
  state.pending_exception = nullptr;
  Value r{};
  if (InvokeMethod(state, bm, nullptr, 0, &r) && state.pending_exception == nullptr &&
      r.ref != nullptr && r.ref->klass != nullptr &&
      r.ref->klass->descriptor == "Ljava/lang/String;") {
    cached = reinterpret_cast<VString*>(r.ref)->ToModifiedUtf8();
  }
  state.pending_exception = saved;
  return cached;
}

// 仅对 appcloud2 的 HTTP 请求头块注入一次 x-app-za（缺省时）。
std::string MaybeInjectZaHeader(VMState& state, std::string bytes) {
  static const char* kMethods[] = {"GET ", "POST ", "PUT ", "DELETE ", "PATCH ", "HEAD "};
  bool is_request = false;
  for (const char* m : kMethods) {
    if (bytes.compare(0, std::strlen(m), m) == 0) {
      is_request = true;
      break;
    }
  }
  if (!is_request) return bytes;
  const size_t head_end = bytes.find("\r\n\r\n");
  if (head_end == std::string::npos) return bytes;
  const std::string head = bytes.substr(0, head_end);
  if (!ContainsIgnoreCase(head, "Host: appcloud2.zhihu.com")) return bytes;
  if (ContainsIgnoreCase(head, "x-app-za:")) return bytes;
  const std::string value = ComputeZaHeader(state);
  if (value.empty()) return bytes;
  const std::string injected = "x-app-za: " + value + "\r\n";
  bytes.insert(head_end + 2, injected);
  __android_log_print(ANDROID_LOG_INFO, "DexVmTls", "inject x-app-za (%zu bytes)",
                      value.size());
  return bytes;
}

}  // namespace
#endif

Value TlsNativeWrite(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  if (n < 4) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "TLS write args");
    *ok = false;
    return Value{};
  }
  const int64_t handle = args[0].l;
  VArray* data = static_cast<VArray*>(args[1].ref);
  const int32_t off = args[2].i;
  const int32_t len = args[3].i;
  if (data == nullptr || off < 0 || len < 0 || off > data->length - len) {
    state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;", "TLS write buffer");
    *ok = false;
    return Value{};
  }
  if (len == 0) return Value{};
  std::string bytes = ReadGuestBytes(data, off, len);
#if defined(__ANDROID__)
  bytes = MaybeInjectZaHeader(state, std::move(bytes));
#endif
  TLS_LOG("guest write begin handle=%lld bytes=%d", static_cast<long long>(handle), len);
  const int64_t id = TlsQueueWrite(handle, bytes);
  TlsReply reply;
  const bool wok = AwaitOk(state, id, 120000, &reply, ok);
  TLS_LOG("guest write handle=%lld bytes=%d %s", static_cast<long long>(handle), len,
          wok ? "OK" : "FAIL");
  return Value{};
}

Value TlsNativeRead(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  if (n < 3) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "TLS read args");
    *ok = false;
    return Value{};
  }
  const int64_t handle = args[0].l;
  int32_t so_timeout_ms = args[1].i;
  int32_t max_bytes = args[2].i;
  if (so_timeout_ms < 0) so_timeout_ms = 0;
  if (max_bytes <= 0) max_bytes = 1;
  if (max_bytes > 256 * 1024) max_bytes = 256 * 1024;
  TLS_LOG("guest read begin handle=%lld max=%d timeout=%d", static_cast<long long>(handle),
          max_bytes, so_timeout_ms);
  const int64_t id = TlsQueueRead(handle, max_bytes, so_timeout_ms);
  TlsReply reply;
  if (!TlsAwaitReply(id, WaitBudget(so_timeout_ms, 120000), &reply)) {
    state.ThrowNew("Ljava/net/SocketTimeoutException;", "TLS bridge read reply timed out");
    *ok = false;
    return Value{};
  }
  if (reply.status == kTlsEof) {
    TLS_LOG("guest read handle=%lld EOF", static_cast<long long>(handle));
    Value r{};  // null = EOF（guest InputStream 映射 -1）
    return r;
  }
  if (reply.status != kTlsOk) {
    TLS_LOG("read handle=%lld failed status=%d: %s", static_cast<long long>(handle),
            reply.status, reply.data.c_str());
    ThrowTlsReply(state, reply);
    *ok = false;
    return Value{};
  }
  TLS_LOG("guest read handle=%lld bytes=%zu", static_cast<long long>(handle), reply.data.size());
  return MakeByteArrayValue(state, reply.data, ok);
}

Value TlsNativeClose(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  if (n < 1) return Value{};
  const int64_t handle = args[0].l;
  TLS_LOG("guest close handle=%lld available=%d", static_cast<long long>(handle),
          TlsBridgeAvailable() ? 1 : 0);
  if (handle == 0 || !TlsBridgeAvailable()) return Value{};
  const int64_t id = TlsQueueClose(handle);
  TlsReply reply;
  // close 尽力而为：宿主回复丢失不向 guest 抛（句柄已从 guest 视角释放）。
  TlsAwaitReply(id, 10000, &reply);
  return Value{};
}

}  // namespace dexvm::runtime
