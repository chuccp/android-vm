// android.app.VmNotifications：guest lib NotificationManager 的宿主通知桥钩子。
// 请求经 core/notification_bridge.cc 队列 -> 宿主 NotificationBridge.java 线程
// 用真 NotificationManager 下发（真系统通知，不伪造）。
#include <string>
#include <utility>

#include "core/builtins_internal.h"
#include "core/framework_builtins_internal.h"
#include "core/intrinsic.h"
#include "core/notification_bridge.h"
#include "core/vm_types.h"

namespace dexvm::runtime {
namespace {

VObject* g_notification_manager = nullptr;

}  // namespace

// NotificationManager 单例（AOSP getSystemService(NOTIFICATION_SERVICE) 同形）。
VObject* AllocNotificationManager(VMState& state) {
  if (g_notification_manager != nullptr) return g_notification_manager;
  bool lib_ok = true;
  VObject* obj = NewLibObject(state, "Landroid/app/NotificationManager;", {}, nullptr, 0,
                              &lib_ok);
  if (obj == nullptr) {
    state.ClearException();
    VClass* klass = state.linker->FindClass("Landroid/app/NotificationManager;");
    if (klass == nullptr) return nullptr;
    obj = state.linker->AllocObject(klass);
  }
  if (obj == nullptr) return nullptr;
  g_notification_manager = obj;
  if (state.heap != nullptr) state.heap->Pin(obj);  // 系统服务缓存即 GC 根
  return obj;
}

// post(int id, String tag, String channel, String title, String text,
//      int flags, int priority)
Value VmNotificationsPost(VMState& state, VObject*, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n < 7) return Value{};
  HostNotificationRequest req;
  req.kind = static_cast<int32_t>(HostNotificationKind::kNotify);
  req.notify_id = static_cast<int64_t>(args[0].i);
  req.tag = Mutf8Arg(args[1]);
  req.channel = Mutf8Arg(args[2]);
  req.title = Mutf8Arg(args[3]);
  req.text = Mutf8Arg(args[4]);
  req.flags = static_cast<int32_t>(args[5].i);
  req.priority = static_cast<int32_t>(args[6].i);
  PushHostNotificationRequest(std::move(req));
  return Value{};
}

// cancel(int id, String tag)
Value VmNotificationsCancel(VMState& state, VObject*, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n < 2) return Value{};
  HostNotificationRequest req;
  req.kind = static_cast<int32_t>(HostNotificationKind::kCancel);
  req.notify_id = static_cast<int64_t>(args[0].i);
  req.tag = Mutf8Arg(args[1]);
  PushHostNotificationRequest(std::move(req));
  return Value{};
}

// cancelAll()
Value VmNotificationsCancelAll(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  HostNotificationRequest req;
  req.kind = static_cast<int32_t>(HostNotificationKind::kCancelAll);
  PushHostNotificationRequest(std::move(req));
  return Value{};
}

// createChannel(String id, String name, int importance)
Value VmNotificationsCreateChannel(VMState& state, VObject*, const Value* args, size_t n,
                                   bool* ok) {
  (void)state;
  *ok = true;
  if (n < 3) return Value{};
  HostNotificationRequest req;
  req.kind = static_cast<int32_t>(HostNotificationKind::kCreateChannel);
  req.channel = Mutf8Arg(args[0]);
  req.channel_name = Mutf8Arg(args[1]);
  req.importance = static_cast<int32_t>(args[2].i);
  PushHostNotificationRequest(std::move(req));
  return Value{};
}

// deleteChannel(String id)
Value VmNotificationsDeleteChannel(VMState& state, VObject*, const Value* args, size_t n,
                                   bool* ok) {
  (void)state;
  *ok = true;
  if (n < 1) return Value{};
  HostNotificationRequest req;
  req.kind = static_cast<int32_t>(HostNotificationKind::kDeleteChannel);
  req.channel = Mutf8Arg(args[0]);
  PushHostNotificationRequest(std::move(req));
  return Value{};
}

}  // namespace dexvm::runtime
