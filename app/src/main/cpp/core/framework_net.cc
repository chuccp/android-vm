// android.net.* 连接性 framework 内建面（桥接优先口径）。
//
// VM 的 socket 栈直接跑在宿主进程系统调用上：宿主在线 = guest 网络可用。
// 本面把该事实投影为 Android ConnectivityManager/NetworkInfo/WifiManager 语义
// （getActiveNetworkInfo/isConnected/getNetworkCapabilities 等）；
// NetworkCallback 注册即回调 onAvailable（宿主网络已就绪）。真实连通性判定
// （DNS/证书/TCP）仍由宿主内核与 TLS 栈完成。
//
// 例外：启动窗口期（BootNetworkOffline，见 core/boot_network.h）投影为断网态。
// 这不是伪造宿主事实，而是 AOSP starting window 口径下“网络尚未就绪”的
// 启动语义——app 的配置/埋点请求直接走本地默认值，避免首帧被网络等待拖住；
// 首帧出现后立即恢复在线投影。
#include "core/builtins_internal.h"
#include "core/boot_network.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/framework_builtins_internal.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "core/vm_types.h"

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace dexvm::runtime {

namespace {
std::atomic<bool> g_boot_network_offline{false};
}  // namespace

void SetBootNetworkOffline(bool offline) {
  g_boot_network_offline.store(offline, std::memory_order_relaxed);
}

bool BootNetworkOffline() {
  return g_boot_network_offline.load(std::memory_order_relaxed);
}

namespace {

constexpr uint32_t kPub = 0x1;
constexpr uint32_t kPriv = 0x2;
constexpr uint32_t kStatic = 0x8;
constexpr uint32_t kFinal = 0x10;
constexpr uint32_t kAbstract = 0x400;
constexpr uint32_t kCtor = 0x10000;

Value NetNoOp(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  return Value{};
}

Value NetZeroInt(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  return r;
}

VClass* AllocClass(ClassLinker* linker) {
  void* mem = linker->heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) return nullptr;
  VClass* k = new (mem) VClass();
  k->status = ClassStatus::kInitialized;
  return k;
}

VClass* MakeClass(ClassLinker* linker, std::vector<VClass*>* all, const char* descriptor,
                  VClass* super, uint32_t instance_bytes, uint32_t access) {
  VClass* k = AllocClass(linker);
  if (k == nullptr) return nullptr;
  k->descriptor = descriptor;
  k->super_class = super;
  k->access_flags = access;
  k->object_size = static_cast<uint32_t>(sizeof(ObjHeader)) + instance_bytes;
  all->push_back(k);
  return k;
}

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

void AddIntrinsicMethod(VClass* klass, const char* name, std::vector<std::string> param_types,
                        const char* return_type, uint32_t access, IntrinsicFn fn) {
  AddMethodSkeleton(klass, name, std::move(param_types), return_type, access);
  klass->direct_methods.back().native_fn = ToIntrinsicSlot(fn);
}

void AddInstanceField(VClass* klass, const char* name, const char* type, uint32_t offset) {
  VField f;
  f.declaring = klass;
  f.name = name;
  f.type_descriptor = type;
  f.access_flags = kPriv;
  f.offset = offset;
  f.is_static = false;
  klass->instance_fields.push_back(std::move(f));
}

void AddStaticField(VClass* klass, const char* name, const char* type) {
  VField f;
  f.declaring = klass;
  f.name = name;
  f.type_descriptor = type;
  f.access_flags = kPriv | kStatic;
  f.offset = static_cast<uint32_t>(klass->static_fields.size());
  f.is_static = true;
  klass->static_fields.push_back(std::move(f));
  Value zero;
  std::memset(&zero, 0, sizeof(zero));
  klass->static_storage.resize(klass->static_fields.size(), zero);
}

void SetStaticInt(VClass* klass, const char* name, int32_t value) {
  for (size_t i = 0; i < klass->static_fields.size(); i++) {
    if (klass->static_fields[i].name == name) {
      klass->static_storage[i].i = value;
      return;
    }
  }
}

void SetStaticRef(VClass* klass, const char* name, ObjHeader* value) {
  for (size_t i = 0; i < klass->static_fields.size(); i++) {
    if (klass->static_fields[i].name == name) {
      klass->static_storage[i].ref = value;
      return;
    }
  }
}

ObjHeader* GetStaticRef(VClass* klass, const char* name) {
  if (klass == nullptr) return nullptr;
  for (size_t i = 0; i < klass->static_fields.size(); i++) {
    if (klass->static_fields[i].name == name) return klass->static_storage[i].ref;
  }
  return nullptr;
}

VField* FindField(VObject* obj, const char* name) {
  if (obj == nullptr || obj->klass == nullptr) return nullptr;
  for (VClass* k = obj->klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == name) return &f;
    }
  }
  return nullptr;
}

int32_t GetIntField(VObject* obj, const char* name) {
  VField* f = FindField(obj, name);
  if (f == nullptr) return 0;
  int32_t v = 0;
  std::memcpy(&v, obj->field_data() + f->offset, sizeof(v));
  return v;
}

void SetIntField(VObject* obj, const char* name, int32_t value) {
  VField* f = FindField(obj, name);
  if (f != nullptr) std::memcpy(obj->field_data() + f->offset, &value, sizeof(value));
}

ObjHeader* GetRefField(VObject* obj, const char* name) {
  VField* f = FindField(obj, name);
  if (f == nullptr) return nullptr;
  ObjHeader* v = nullptr;
  std::memcpy(&v, obj->field_data() + f->offset, sizeof(v));
  return v;
}

void SetRefField(VObject* obj, const char* name, ObjHeader* value) {
  VField* f = FindField(obj, name);
  if (f != nullptr) std::memcpy(obj->field_data() + f->offset, &value, sizeof(value));
}

ObjHeader* NewString(ClassLinker* linker, const std::string& text) {
  std::u16string utf16(text.begin(), text.end());
  return reinterpret_cast<ObjHeader*>(linker->AllocString(utf16));
}

VObject* Alloc(VMState& state, const char* descriptor) {
  VClass* k = state.linker->FindClass(descriptor);
  if (k == nullptr) return nullptr;
  return state.linker->AllocObject(k);
}

Value MakeIntArray(VMState& state, const std::vector<int32_t>& values) {
  Value r{};
  VClass* arr_cls =
      state.linker->FindArrayClass(state.linker->PrimitiveClass('I'));
  if (arr_cls == nullptr) return r;
  VArray* arr = state.linker->AllocArray(arr_cls, static_cast<int32_t>(values.size()));
  if (arr == nullptr) return r;
  for (size_t i = 0; i < values.size(); i++) {
    std::memcpy(arr->data() + i * 8, &values[i], sizeof(int32_t));
  }
  r.ref = arr;
  return r;
}

VObject* NewNetworkInfo(VMState& state, int32_t type);
VObject* NewNetwork(VMState& state, int32_t net_id);
VObject* NewNetworkCapabilities(VMState& state);
VObject* NewLinkProperties(VMState& state);
VObject* NewWifiInfo(VMState& state);
VObject* NewNetworkRequest(VMState& state);
VObject* NewConnectivityManager(VMState& state);

// ---------------------------------------------------------------------------
// NetworkInfo / State / DetailedState
// ---------------------------------------------------------------------------

Value NetEnumName(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = GetRefField(self, "name");
  if (r.ref == nullptr) {
    r.ref = NewString(state.linker, "UNKNOWN");
  }
  return r;
}

Value NetEnumToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return NetEnumName(state, self, args, n, ok);
}

Value NetworkInfoIsConnected(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = 1;
  return r;
}

Value NetworkInfoGetType(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = GetIntField(self, "mType");
  return r;
}

Value NetworkInfoGetSubtype(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = GetIntField(self, "mSubtype");
  return r;
}

Value NetworkInfoGetStringField(VMState& state, VObject* self, const char* name, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.ref = GetRefField(self, name);
  return r;
}

Value NetworkInfoGetTypeName(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  return NetworkInfoGetStringField(state, self, "mTypeName", ok);
}
Value NetworkInfoGetSubtypeName(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  return NetworkInfoGetStringField(state, self, "mSubtypeName", ok);
}
Value NetworkInfoGetExtraInfo(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  return NetworkInfoGetStringField(state, self, "mExtraInfo", ok);
}
Value NetworkInfoGetReason(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  return NetworkInfoGetStringField(state, self, "mReason", ok);
}

Value NetworkInfoGetState(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = GetStaticRef(state.linker->FindClass("Landroid/net/NetworkInfo$State;"), "CONNECTED");
  return r;
}

Value NetworkInfoGetDetailedState(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = GetStaticRef(state.linker->FindClass("Landroid/net/NetworkInfo$DetailedState;"),
                       "CONNECTED");
  return r;
}

Value NetworkInfoToString(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  std::string text = "NetworkInfo: type: ";
  ObjHeader* type_name = GetRefField(self, "mTypeName");
  text += type_name != nullptr ? static_cast<VString*>(type_name)->ToModifiedUtf8() : "?";
  text += ", state: CONNECTED/CONNECTED, reason: (unspecified), extra: ";
  ObjHeader* extra = GetRefField(self, "mExtraInfo");
  text += extra != nullptr ? static_cast<VString*>(extra)->ToModifiedUtf8() : "";
  r.ref = NewString(state.linker, text);
  return r;
}

Value NetworkInfoInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  int32_t type = n > 0 ? args[0].i : 1;  // TYPE_WIFI
  SetIntField(self, "mType", type);
  SetIntField(self, "mSubtype", 0);
  SetRefField(self, "mTypeName",
              NewString(state.linker, type == 1 ? "WIFI" : "MOBILE"));
  SetRefField(self, "mSubtypeName", NewString(state.linker, ""));
  SetRefField(self, "mExtraInfo", NewString(state.linker, type == 1 ? "wifi" : "mobile"));
  SetRefField(self, "mReason", NewString(state.linker, "(unspecified)"));
  return Value{};
}

VObject* NewNetworkInfo(VMState& state, int32_t type) {
  VObject* info = Alloc(state, "Landroid/net/NetworkInfo;");
  if (info == nullptr) return nullptr;
  SetIntField(info, "mType", type);
  SetIntField(info, "mSubtype", 0);
  SetRefField(info, "mTypeName", NewString(state.linker, type == 1 ? "WIFI" : "MOBILE"));
  SetRefField(info, "mSubtypeName", NewString(state.linker, ""));
  SetRefField(info, "mExtraInfo", NewString(state.linker, type == 1 ? "wifi" : "mobile"));
  SetRefField(info, "mReason", NewString(state.linker, "(unspecified)"));
  return info;
}

// ---------------------------------------------------------------------------
// Network / NetworkCapabilities / LinkProperties / LinkAddress / RouteInfo
// ---------------------------------------------------------------------------

Value NetworkInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  SetIntField(self, "netId", n > 0 ? args[0].i : 1);
  return Value{};
}

Value NetworkGetNetworkHandle(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.l = 0x100000000LL + GetIntField(self, "netId");
  return r;
}

Value NetworkToString(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.ref = NewString(state.linker, "Network(" + std::to_string(GetIntField(self, "netId")) + ")");
  return r;
}

Value ObjectEquals(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = n > 0 && args[0].ref == reinterpret_cast<ObjHeader*>(self) ? 1 : 0;
  return r;
}

Value ObjectHashCode(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = static_cast<int32_t>(reinterpret_cast<uintptr_t>(self) & 0x7FFFFFFF);
  return r;
}

Value CapHasCapability(VMState& state, VObject*, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  int32_t cap = n > 0 ? args[0].i : -1;
  // 宿主在线：INTERNET/NOT_RESTRICTED/NOT_ROAMING/TRUSTED/NOT_VPN/VALIDATED
  bool has = cap == 12 || cap == 11 || cap == 13 || cap == 14 || cap == 15 || cap == 16;
  Value r{};
  r.i = has ? 1 : 0;
  return r;
}

Value CapHasTransport(VMState& state, VObject*, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  int32_t transport = n > 0 ? args[0].i : -1;
  Value r{};
  r.i = transport == 1 ? 1 : 0;  // TRANSPORT_WIFI
  return r;
}

Value CapGetCapabilities(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  return MakeIntArray(state, {12, 11, 13, 14, 15, 16});
}

Value CapGetTransportTypes(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  return MakeIntArray(state, {1});
}

Value CapGetBandwidth(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = 100000;
  return r;
}

Value CapToString(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = NewString(state.linker, "NetworkCapabilities[WIFI, INTERNET, VALIDATED]");
  return r;
}

VObject* NewNetwork(VMState& state, int32_t net_id) {
  VObject* network = Alloc(state, "Landroid/net/Network;");
  if (network == nullptr) return nullptr;
  SetIntField(network, "netId", net_id);
  return network;
}

VObject* NewNetworkCapabilities(VMState& state) {
  return Alloc(state, "Landroid/net/NetworkCapabilities;");
}

VObject* NewLinkProperties(VMState& state) {
  return Alloc(state, "Landroid/net/LinkProperties;");
}

Value LinkGetInterfaceName(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = NewString(state.linker, "wlan0");
  return r;
}

Value LinkGetEmptyArray(VMState& state, VObject*, const Value*, size_t, bool* ok,
                        const char* element_desc) {
  *ok = true;
  Value r{};
  VClass* elem = state.linker->FindClass(element_desc);
  if (elem == nullptr) return r;
  VClass* arr_cls = state.linker->FindArrayClass(elem);
  if (arr_cls == nullptr) return r;
  r.ref = state.linker->AllocArray(arr_cls, 0);
  return r;
}

Value LinkGetLinkAddresses(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  return LinkGetEmptyArray(state, nullptr, nullptr, 0, ok, "Landroid/net/LinkAddress;");
}
Value LinkGetRoutes(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  return LinkGetEmptyArray(state, nullptr, nullptr, 0, ok, "Landroid/net/RouteInfo;");
}
Value LinkGetDnsServers(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  return LinkGetEmptyArray(state, nullptr, nullptr, 0, ok, "Ljava/net/InetAddress;");
}
Value LinkGetHttpProxy(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  return Value{};
}
Value LinkToString(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = NewString(state.linker, "LinkProperties {InterfaceName: wlan0}");
  return r;
}

// ---------------------------------------------------------------------------
// ConnectivityManager / NetworkRequest / NetworkCallback
// ---------------------------------------------------------------------------

Value CallbackNotify(VMState& state, VObject* callback, const char* name,
                     std::vector<std::string> params, const Value* args, size_t n) {
  Value out{};
  if (!InvokeVirtualNamed(state, callback, name, std::move(params), "V", args, n, &out)) {
    state.ClearException();
  }
  return out;
}

void NotifyAvailable(VMState& state, VObject* callback) {
  if (callback == nullptr) return;
  VObject* network = NewNetwork(state, 1);
  VObject* caps = NewNetworkCapabilities(state);
  VObject* link = NewLinkProperties(state);
  Value a1[1] = {};
  a1[0].ref = network;
  CallbackNotify(state, callback, "onAvailable", {"Landroid/net/Network;"}, a1, 1);
  // API 26+ 重载：onAvailable(long networkHandle)
  Value ah[1] = {};
  ah[0].l = 0x100000000LL + 1;
  CallbackNotify(state, callback, "onAvailable", {"J"}, ah, 1);
  Value a2[2] = {};
  a2[0].ref = network;
  a2[1].ref = caps;
  CallbackNotify(state, callback, "onCapabilitiesChanged",
                 {"Landroid/net/Network;", "Landroid/net/NetworkCapabilities;"}, a2, 2);
  Value a3[2] = {};
  a3[0].ref = network;
  a3[1].ref = link;
  CallbackNotify(state, callback, "onLinkPropertiesChanged",
                 {"Landroid/net/Network;", "Landroid/net/LinkProperties;"}, a3, 2);
}

Value CMGetActiveNetworkInfo(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  if (BootNetworkOffline()) return r;  // 启动窗口期：无活动网络（AOSP 断网语义）
  r.ref = NewNetworkInfo(state, 1);  // TYPE_WIFI
  return r;
}

Value CMGetNetworkInfo(VMState& state, VObject*, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (BootNetworkOffline()) return r;
  int32_t type = n > 0 ? args[0].i : 1;
  if (type != 0 && type != 1 && type != 9) return r;  // MOBILE/WIFI/ETHERNET 之外 null
  r.ref = NewNetworkInfo(state, type == 0 ? 0 : 1);
  return r;
}

Value CMGetAllNetworks(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  VClass* network_cls = state.linker->FindClass("Landroid/net/Network;");
  if (network_cls == nullptr) return r;
  VClass* arr_cls = state.linker->FindArrayClass(network_cls);
  if (arr_cls == nullptr) return r;
  if (BootNetworkOffline()) {
    r.ref = state.linker->AllocArray(arr_cls, 0);
    return r;
  }
  VArray* arr = state.linker->AllocArray(arr_cls, 1);
  if (arr == nullptr) return r;
  ObjHeader* network = NewNetwork(state, 1);
  std::memcpy(arr->data(), &network, sizeof(network));
  r.ref = arr;
  return r;
}

Value CMGetActiveNetwork(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  if (BootNetworkOffline()) return r;
  r.ref = NewNetwork(state, 1);
  return r;
}

Value CMGetNetworkCapabilities(VMState& state, VObject*, const Value* args, size_t n,
                               bool* ok) {
  *ok = true;
  Value r{};
  // AOSP：network 为 null 或启动窗口期无网时返回 null。
  if (BootNetworkOffline() || n < 1 || args[0].ref == nullptr) return r;
  r.ref = NewNetworkCapabilities(state);
  return r;
}

Value CMGetLinkProperties(VMState& state, VObject*, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (BootNetworkOffline() || n < 1 || args[0].ref == nullptr) return r;
  r.ref = NewLinkProperties(state);
  return r;
}

Value CMIsMetered(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = BootNetworkOffline() ? 1 : 0;  // 无默认网络时 AOSP 返回 true
  return r;
}

Value CMIsDefaultNetworkActive(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = BootNetworkOffline() ? 0 : 1;
  return r;
}

Value CMRegisterCallback(VMState& state, VObject*, const Value* args, size_t n, bool* ok) {
  *ok = true;
  // 启动窗口期无网络：只注册不回调（AOSP 无默认网络时不会 onAvailable）。
  if (BootNetworkOffline()) return Value{};
  // (NetworkRequest, NetworkCallback[, Handler]) / (NetworkCallback)
  VObject* callback = nullptr;
  for (size_t i = 0; i < n; i++) {
    VObject* candidate = static_cast<VObject*>(args[i].ref);
    if (candidate != nullptr && candidate->klass != nullptr &&
        candidate->klass->descriptor.find("NetworkCallback") != std::string::npos) {
      callback = candidate;
      break;
    }
  }
  if (callback == nullptr && n > 0) callback = static_cast<VObject*>(args[n - 1].ref);
  NotifyAvailable(state, callback);
  return Value{};
}

Value CMUnregisterCallback(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  return Value{};
}

Value CMStartUsingFeature(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = 0;  // PhoneConstants.APN_ALREADY_ACTIVE
  return r;
}

Value NetworkRequestInit(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  return Value{};
}

Value NetworkRequestBuilderAdd(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.ref = self;
  return r;
}

Value NetworkRequestBuilderBuild(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = NewNetworkRequest(state);
  return r;
}

VObject* NewNetworkRequest(VMState& state) {
  return Alloc(state, "Landroid/net/NetworkRequest;");
}

// ---------------------------------------------------------------------------
// WifiManager / WifiInfo
// ---------------------------------------------------------------------------

Value WifiGetConnectionInfo(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = NewWifiInfo(state);
  return r;
}

Value WifiIsEnabled(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = BootNetworkOffline() ? 0 : 1;
  return r;
}

Value WifiGetState(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = BootNetworkOffline() ? 1 : 3;  // BOOT: WIFI_STATE_DISABLED；在线: ENABLED
  return r;
}

Value WifiInfoGetSSID(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = NewString(state.linker, "\"DexVmWifi\"");
  return r;
}

Value WifiInfoGetBSSID(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = NewString(state.linker, "02:00:00:00:00:00");
  return r;
}

Value WifiInfoGetMac(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = NewString(state.linker, "02:00:00:00:00:00");
  return r;
}

Value ReturnMinusOne(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = -1;
  return r;
}

Value ReturnNegative55(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = -55;
  return r;
}

Value Return866(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = 866;
  return r;
}

Value Return5180(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = 5180;
  return r;
}

Value WifiInfoToString(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = NewString(state.linker, "WifiInfo{SSID=\"DexVmWifi\", BSSID=02:00:00:00:00:00}");
  return r;
}

VObject* NewWifiInfo(VMState& state) {
  return Alloc(state, "Landroid/net/wifi/WifiInfo;");
}

VObject* NewConnectivityManager(VMState& state) {
  return Alloc(state, "Landroid/net/ConnectivityManager;");
}

// 枚举类（NetworkInfo$State / $DetailedState）：实例 name 字段 + 静态常量对象。
void RegisterEnumClass(ClassLinker* linker, std::vector<VClass*>* all, const char* descriptor,
                       const char* const* names, size_t count) {
  VClass* object = linker->FindClass("Ljava/lang/Object;");
  VClass* k = MakeClass(linker, all, descriptor, object, 8, kPub | kFinal);
  if (k == nullptr) return;
  AddInstanceField(k, "name", "Ljava/lang/String;", 0);
  AddMethodSkeleton(k, "<init>", {}, "V", kPriv | kCtor);
  AddIntrinsicMethod(k, "name", {}, "Ljava/lang/String;", kPub, EnumName);
  AddIntrinsicMethod(k, "toString", {}, "Ljava/lang/String;", kPub, EnumToString);
  AddIntrinsicMethod(k, "ordinal", {}, "I", kPub, ReturnMinusOne);
  for (size_t i = 0; i < count; i++) AddStaticField(k, names[i], descriptor);
  for (size_t i = 0; i < count; i++) {
    VObject* value = linker->AllocObject(k);
    if (value == nullptr) continue;
    SetRefField(value, "name", NewString(linker, names[i]));
    linker->heap()->Pin(value);
    SetStaticRef(k, names[i], reinterpret_cast<ObjHeader*>(value));
  }
}

}  // namespace

VObject* AllocConnectivityManager(VMState& state) { return NewConnectivityManager(state); }

VObject* AllocWifiManager(VMState& state) {
  return Alloc(state, "Landroid/net/wifi/WifiManager;");
}

void RegisterNetworkBuiltins(ClassLinker* linker, std::vector<VClass*>* all) {
  if (linker == nullptr || all == nullptr) return;
  VClass* object = linker->FindClass("Ljava/lang/Object;");
  if (object == nullptr) return;

  // ---- NetworkInfo ----
  VClass* network_info =
      MakeClass(linker, all, "Landroid/net/NetworkInfo;", object, 48, kPub);
  if (network_info != nullptr) {
    AddInstanceField(network_info, "mType", "I", 0);
    AddInstanceField(network_info, "mSubtype", "I", 8);
    AddInstanceField(network_info, "mTypeName", "Ljava/lang/String;", 16);
    AddInstanceField(network_info, "mSubtypeName", "Ljava/lang/String;", 24);
    AddInstanceField(network_info, "mExtraInfo", "Ljava/lang/String;", 32);
    AddInstanceField(network_info, "mReason", "Ljava/lang/String;", 40);
    AddIntrinsicMethod(network_info, "<init>", {}, "V", kPub | kCtor, NetworkInfoInit);
    AddIntrinsicMethod(network_info, "<init>", {"I"}, "V", kPub | kCtor, NetworkInfoInit);
    AddMethodSkeleton(network_info, "toString", {}, "Ljava/lang/String;", kPub);
    AddIntrinsicMethod(network_info, "toString", {}, "Ljava/lang/String;", kPub,
                       NetworkInfoToString);
    AddIntrinsicMethod(network_info, "isConnected", {}, "Z", kPub, NetworkInfoIsConnected);
    AddIntrinsicMethod(network_info, "isConnectedOrConnecting", {}, "Z", kPub,
                       NetworkInfoIsConnected);
    AddIntrinsicMethod(network_info, "isAvailable", {}, "Z", kPub, NetworkInfoIsConnected);
    AddIntrinsicMethod(network_info, "isFailover", {}, "Z", kPub, NetZeroInt);
    AddIntrinsicMethod(network_info, "isRoaming", {}, "Z", kPub, NetZeroInt);
    AddIntrinsicMethod(network_info, "getType", {}, "I", kPub, NetworkInfoGetType);
    AddIntrinsicMethod(network_info, "getSubtype", {}, "I", kPub, NetworkInfoGetSubtype);
    AddIntrinsicMethod(network_info, "getTypeName", {}, "Ljava/lang/String;", kPub,
                       NetworkInfoGetTypeName);
    AddIntrinsicMethod(network_info, "getSubtypeName", {}, "Ljava/lang/String;", kPub,
                       NetworkInfoGetSubtypeName);
    AddIntrinsicMethod(network_info, "getExtraInfo", {}, "Ljava/lang/String;", kPub,
                       NetworkInfoGetExtraInfo);
    AddIntrinsicMethod(network_info, "getReason", {}, "Ljava/lang/String;", kPub,
                       NetworkInfoGetReason);
    AddIntrinsicMethod(network_info, "getState", {}, "Landroid/net/NetworkInfo$State;", kPub,
                       NetworkInfoGetState);
    AddIntrinsicMethod(network_info, "getDetailedState", {},
                       "Landroid/net/NetworkInfo$DetailedState;", kPub,
                       NetworkInfoGetDetailedState);
  }

  const char* state_names[] = {"CONNECTED", "CONNECTING", "DISCONNECTED",
                               "DISCONNECTING", "SUSPENDED", "UNKNOWN"};
  RegisterEnumClass(linker, all, "Landroid/net/NetworkInfo$State;", state_names, 6);
  const char* detailed_names[] = {"IDLE",          "SCANNING",   "CONNECTING",
                                  "AUTHENTICATING", "OBTAINING_IPADDR", "CONNECTED",
                                  "SUSPENDED",     "DISCONNECTING", "DISCONNECTED",
                                  "FAILED",        "BLOCKED",    "VERIFYING_POOR_LINK",
                                  "CAPTIVE_PORTAL_CHECK"};
  RegisterEnumClass(linker, all, "Landroid/net/NetworkInfo$DetailedState;", detailed_names,
                    13);

  // ---- Network ----
  VClass* network = MakeClass(linker, all, "Landroid/net/Network;", object, 8, kPub);
  if (network != nullptr) {
    AddInstanceField(network, "netId", "I", 0);
    AddIntrinsicMethod(network, "<init>", {}, "V", kPub | kCtor, NetworkInit);
    AddIntrinsicMethod(network, "<init>", {"I"}, "V", kPub | kCtor, NetworkInit);
    AddIntrinsicMethod(network, "getNetworkHandle", {}, "J", kPub, NetworkGetNetworkHandle);
    AddIntrinsicMethod(network, "toString", {}, "Ljava/lang/String;", kPub, NetworkToString);
    AddIntrinsicMethod(network, "equals", {"Ljava/lang/Object;"}, "Z", kPub, ObjectEquals);
    AddIntrinsicMethod(network, "hashCode", {}, "I", kPub, ObjectHashCode);
  }

  // ---- NetworkCapabilities ----
  VClass* caps =
      MakeClass(linker, all, "Landroid/net/NetworkCapabilities;", object, 0, kPub);
  if (caps != nullptr) {
    const char* int_consts[] = {"TRANSPORT_CELLULAR", "TRANSPORT_WIFI", "TRANSPORT_BLUETOOTH",
                                "TRANSPORT_ETHERNET", "TRANSPORT_VPN"};
    for (size_t i = 0; i < 5; i++) AddStaticField(caps, int_consts[i], "I");
    for (size_t i = 0; i < 5; i++) SetStaticInt(caps, int_consts[i], static_cast<int32_t>(i));
    AddIntrinsicMethod(caps, "<init>", {}, "V", kPub | kCtor, NetNoOp);
    AddIntrinsicMethod(caps, "hasCapability", {"I"}, "Z", kPub, CapHasCapability);
    AddIntrinsicMethod(caps, "hasTransport", {"I"}, "Z", kPub, CapHasTransport);
    AddIntrinsicMethod(caps, "getCapabilities", {}, "[I", kPub, CapGetCapabilities);
    AddIntrinsicMethod(caps, "getTransportTypes", {}, "[I", kPub, CapGetTransportTypes);
    AddIntrinsicMethod(caps, "getLinkDownstreamBandwidthKbps", {}, "I", kPub, CapGetBandwidth);
    AddIntrinsicMethod(caps, "getLinkUpstreamBandwidthKbps", {}, "I", kPub, CapGetBandwidth);
    AddIntrinsicMethod(caps, "toString", {}, "Ljava/lang/String;", kPub, CapToString);
  }

  // ---- LinkProperties / LinkAddress / RouteInfo ----
  VClass* link = MakeClass(linker, all, "Landroid/net/LinkProperties;", object, 0, kPub);
  if (link != nullptr) {
    AddIntrinsicMethod(link, "<init>", {}, "V", kPub | kCtor, NetNoOp);
    AddIntrinsicMethod(link, "getInterfaceName", {}, "Ljava/lang/String;", kPub,
                       LinkGetInterfaceName);
    AddIntrinsicMethod(link, "getLinkAddresses", {}, "Ljava/util/List;", kPub,
                       LinkGetLinkAddresses);
    AddIntrinsicMethod(link, "getRoutes", {}, "Ljava/util/List;", kPub, LinkGetRoutes);
    AddIntrinsicMethod(link, "getDnsServers", {}, "Ljava/util/List;", kPub, LinkGetDnsServers);
    AddIntrinsicMethod(link, "getHttpProxy", {}, "Landroid/net/ProxyInfo;", kPub,
                       LinkGetHttpProxy);
    AddIntrinsicMethod(link, "toString", {}, "Ljava/lang/String;", kPub, LinkToString);
  }
  MakeClass(linker, all, "Landroid/net/LinkAddress;", object, 0, kPub);
  MakeClass(linker, all, "Landroid/net/RouteInfo;", object, 0, kPub);

  // ---- NetworkRequest / Builder ----
  VClass* request = MakeClass(linker, all, "Landroid/net/NetworkRequest;", object, 0, kPub);
  if (request != nullptr) {
    AddIntrinsicMethod(request, "<init>", {}, "V", kPub | kCtor, NetworkRequestInit);
  }
  VClass* builder =
      MakeClass(linker, all, "Landroid/net/NetworkRequest$Builder;", object, 0, kPub);
  if (builder != nullptr) {
    AddIntrinsicMethod(builder, "<init>", {}, "V", kPub | kCtor, NetworkRequestInit);
    for (const char* name : {"addCapability", "removeCapability", "addTransportType",
                             "removeTransportType"}) {
      AddIntrinsicMethod(builder, name, {"I"}, "Landroid/net/NetworkRequest$Builder;", kPub,
                         NetworkRequestBuilderAdd);
    }
    AddIntrinsicMethod(builder, "build", {}, "Landroid/net/NetworkRequest;", kPub,
                       NetworkRequestBuilderBuild);
  }

  // ---- ConnectivityManager + NetworkCallback ----
  VClass* cm =
      MakeClass(linker, all, "Landroid/net/ConnectivityManager;", object, 0, kPub);
  if (cm != nullptr) {
    const char* type_consts[] = {
        "TYPE_MOBILE", "TYPE_WIFI", "TYPE_MOBILE_MMS", "TYPE_MOBILE_SUPL",
        "TYPE_MOBILE_DUN", "TYPE_MOBILE_HIPRI", "TYPE_WIMAX", "TYPE_BLUETOOTH",
        "TYPE_DUMMY", "TYPE_ETHERNET", "TYPE_MOBILE_FOTA", "TYPE_MOBILE_IMS",
        "TYPE_MOBILE_CBS", "TYPE_WIFI_P2P", "TYPE_MOBILE_IA", "TYPE_MOBILE_EMERGENCY",
        "TYPE_PROXY", "TYPE_VPN"};
    for (size_t i = 0; i < 18; i++) AddStaticField(cm, type_consts[i], "I");
    for (size_t i = 0; i < 18; i++) SetStaticInt(cm, type_consts[i], static_cast<int32_t>(i));
    AddIntrinsicMethod(cm, "<init>", {}, "V", kPub | kCtor, NetNoOp);
    AddIntrinsicMethod(cm, "getActiveNetworkInfo", {}, "Landroid/net/NetworkInfo;", kPub,
                       CMGetActiveNetworkInfo);
    AddIntrinsicMethod(cm, "getNetworkInfo", {"I"}, "Landroid/net/NetworkInfo;", kPub,
                       CMGetNetworkInfo);
    AddIntrinsicMethod(cm, "getAllNetworks", {}, "[Landroid/net/Network;", kPub,
                       CMGetAllNetworks);
    AddIntrinsicMethod(cm, "getActiveNetwork", {}, "Landroid/net/Network;", kPub,
                       CMGetActiveNetwork);
    AddIntrinsicMethod(cm, "getNetworkCapabilities", {"Landroid/net/Network;"},
                       "Landroid/net/NetworkCapabilities;", kPub, CMGetNetworkCapabilities);
    AddIntrinsicMethod(cm, "getLinkProperties", {"Landroid/net/Network;"},
                       "Landroid/net/LinkProperties;", kPub, CMGetLinkProperties);
    AddIntrinsicMethod(cm, "isActiveNetworkMetered", {}, "Z", kPub, CMIsMetered);
    AddIntrinsicMethod(cm, "isDefaultNetworkActive", {}, "Z", kPub, CMIsDefaultNetworkActive);
    AddIntrinsicMethod(cm, "registerNetworkCallback",
                       {"Landroid/net/NetworkRequest;",
                        "Landroid/net/ConnectivityManager$NetworkCallback;"},
                       "V", kPub, CMRegisterCallback);
    AddIntrinsicMethod(cm, "registerNetworkCallback",
                       {"Landroid/net/NetworkRequest;",
                        "Landroid/net/ConnectivityManager$NetworkCallback;",
                        "Landroid/os/Handler;"},
                       "V", kPub, CMRegisterCallback);
    AddIntrinsicMethod(cm, "registerDefaultNetworkCallback",
                       {"Landroid/net/ConnectivityManager$NetworkCallback;"}, "V", kPub,
                       CMRegisterCallback);
    AddIntrinsicMethod(cm, "unregisterNetworkCallback",
                       {"Landroid/net/ConnectivityManager$NetworkCallback;"}, "V", kPub,
                       CMUnregisterCallback);
    AddIntrinsicMethod(cm, "requestNetwork",
                       {"Landroid/net/NetworkRequest;",
                        "Landroid/net/ConnectivityManager$NetworkCallback;"},
                       "V", kPub, CMRegisterCallback);
    AddIntrinsicMethod(cm, "requestNetwork",
                       {"Landroid/net/NetworkRequest;",
                        "Landroid/net/ConnectivityManager$NetworkCallback;", "I"},
                       "V", kPub, CMRegisterCallback);
    AddIntrinsicMethod(cm, "startUsingNetworkFeature", {"I", "Ljava/lang/String;"}, "I", kPub,
                       CMStartUsingFeature);
    AddIntrinsicMethod(cm, "stopUsingNetworkFeature", {"I", "Ljava/lang/String;"}, "I", kPub,
                       CMStartUsingFeature);
  }
  VClass* callback = MakeClass(linker, all,
                               "Landroid/net/ConnectivityManager$NetworkCallback;", object, 0,
                               kPub);
  if (callback != nullptr) {
    AddIntrinsicMethod(callback, "<init>", {}, "V", kPub | kCtor, NetNoOp);
    AddIntrinsicMethod(callback, "onAvailable", {"Landroid/net/Network;"}, "V", kPub, NetNoOp);
    AddIntrinsicMethod(callback, "onAvailable", {"J"}, "V", kPub, NetNoOp);
    AddIntrinsicMethod(callback, "onLost", {"Landroid/net/Network;"}, "V", kPub, NetNoOp);
    AddIntrinsicMethod(callback, "onUnavailable", {}, "V", kPub, NetNoOp);
    AddIntrinsicMethod(callback, "onCapabilitiesChanged",
                       {"Landroid/net/Network;", "Landroid/net/NetworkCapabilities;"}, "V",
                       kPub, NetNoOp);
    AddIntrinsicMethod(callback, "onLinkPropertiesChanged",
                       {"Landroid/net/Network;", "Landroid/net/LinkProperties;"}, "V", kPub,
                       NetNoOp);
    AddIntrinsicMethod(callback, "onLosing", {"Landroid/net/Network;", "I"}, "V", kPub,
                       NetNoOp);
  }

  // ---- WifiManager / WifiInfo ----
  VClass* wifi =
      MakeClass(linker, all, "Landroid/net/wifi/WifiManager;", object, 0, kPub);
  if (wifi != nullptr) {
    AddStaticField(wifi, "WIFI_STATE_DISABLED", "I");
    AddStaticField(wifi, "WIFI_STATE_DISABLING", "I");
    AddStaticField(wifi, "WIFI_STATE_ENABLED", "I");
    AddStaticField(wifi, "WIFI_STATE_ENABLING", "I");
    AddStaticField(wifi, "WIFI_STATE_UNKNOWN", "I");
    SetStaticInt(wifi, "WIFI_STATE_DISABLED", 1);
    SetStaticInt(wifi, "WIFI_STATE_DISABLING", 0);
    SetStaticInt(wifi, "WIFI_STATE_ENABLED", 3);
    SetStaticInt(wifi, "WIFI_STATE_ENABLING", 2);
    SetStaticInt(wifi, "WIFI_STATE_UNKNOWN", 4);
    AddIntrinsicMethod(wifi, "<init>", {}, "V", kPub | kCtor, NetNoOp);
    AddIntrinsicMethod(wifi, "getConnectionInfo", {}, "Landroid/net/wifi/WifiInfo;", kPub,
                       WifiGetConnectionInfo);
    AddIntrinsicMethod(wifi, "isWifiEnabled", {}, "Z", kPub, WifiIsEnabled);
    AddIntrinsicMethod(wifi, "setWifiEnabled", {"Z"}, "Z", kPub, WifiIsEnabled);
    AddIntrinsicMethod(wifi, "getWifiState", {}, "I", kPub, WifiGetState);
  }
  VClass* wifi_info =
      MakeClass(linker, all, "Landroid/net/wifi/WifiInfo;", object, 0, kPub);
  if (wifi_info != nullptr) {
    AddIntrinsicMethod(wifi_info, "<init>", {}, "V", kPub | kCtor, NetNoOp);
    AddIntrinsicMethod(wifi_info, "getSSID", {}, "Ljava/lang/String;", kPub,
                       WifiInfoGetSSID);
    AddIntrinsicMethod(wifi_info, "getBSSID", {}, "Ljava/lang/String;", kPub,
                       WifiInfoGetBSSID);
    AddIntrinsicMethod(wifi_info, "getMacAddress", {}, "Ljava/lang/String;", kPub,
                       WifiInfoGetMac);
    AddIntrinsicMethod(wifi_info, "getNetworkId", {}, "I", kPub, ReturnMinusOne);
    AddIntrinsicMethod(wifi_info, "getRssi", {}, "I", kPub, ReturnNegative55);
    AddIntrinsicMethod(wifi_info, "getLinkSpeed", {}, "I", kPub, Return866);
    AddIntrinsicMethod(wifi_info, "getFrequency", {}, "I", kPub, Return5180);
    AddIntrinsicMethod(wifi_info, "toString", {}, "Ljava/lang/String;", kPub,
                       WifiInfoToString);
  }
}

}  // namespace dexvm::runtime
