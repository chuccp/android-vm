// java.lang.ref Reference 族（Reference/WeakReference/ReferenceQueue 面）。
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

// ---- java.lang.ref 链（ThreadLocalMap.Entry extends WeakReference）----
// 语义：单线程 VM 无引用处理线程——referent 视为强引用（GC 不清扫），
// enqueue/isEnqueued 恒 false（无 pending 链）。
Value RefInit1(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)ok;
  ObjHeader** referent = reinterpret_cast<ObjHeader**>(self->field_data() + 0);
  *referent = n >= 1 ? args[0].ref : nullptr;
  ObjHeader** queue = reinterpret_cast<ObjHeader**>(self->field_data() + 8);
  VClass* rq = state.linker->FindClass("Ljava/lang/ref/ReferenceQueue;");
  *queue = (rq != nullptr && !rq->static_storage.empty()) ? rq->static_storage[0].ref : nullptr;
  return Value{};
}

Value RefInit2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)ok;
  ObjHeader** referent = reinterpret_cast<ObjHeader**>(self->field_data() + 0);
  *referent = n >= 1 ? args[0].ref : nullptr;
  ObjHeader** queue = reinterpret_cast<ObjHeader**>(self->field_data() + 8);
  if (n >= 2 && args[1].ref != nullptr) {
    *queue = args[1].ref;
  } else {
    VClass* rq = state.linker->FindClass("Ljava/lang/ref/ReferenceQueue;");
    *queue = (rq != nullptr && !rq->static_storage.empty()) ? rq->static_storage[0].ref : nullptr;
  }
  return Value{};
}

Value RefGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  std::memcpy(&r.ref, self->field_data() + 0, sizeof(r.ref));
  return r;
}

Value RefClear(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  ObjHeader** referent = reinterpret_cast<ObjHeader**>(self->field_data() + 0);
  *referent = nullptr;
  return Value{};
}

Value RefFalse(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = 0;
  return r;
}

Value RefNoop(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  return Value{};
}

Value RefQueuePoll(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  return Value{};  // null
}

}  // namespace dexvm::runtime
