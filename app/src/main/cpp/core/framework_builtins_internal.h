// core/framework_builtins_internal.h —— framework 族 TU 拆分共享面（纯移动重构
// 配套）。跨 TU 引用的 framework 内建面 / 共享助手 / VM 侧状态全局量声明；定义在
// core/framework_*.cc。仅供 core/ 内部使用。（跨族助手 Mutf8Arg/CallVirtual/
// GetIField/SetIField 用 core/builtins_internal.h 的共享定义。）
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/framework_builtins.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "core/vm_types.h"

namespace dexvm::runtime {

// ---- 跨 TU 共享助手与 framework 内建面（定义见各 framework_*.cc）----
// android.net.* 连接性面（framework_net.cc）：ConnectivityManager/NetworkInfo/
// NetworkCapabilities/WifiManager 等（宿主在线投影；TLS 上游网络初始化依赖）。
void RegisterNetworkBuiltins(ClassLinker* linker, std::vector<VClass*>* all);
// Context.getSystemService("connectivity"/"wifi") 落点（AOSP 服务单例语义投影）。
VObject* AllocConnectivityManager(VMState& state);
VObject* AllocWifiManager(VMState& state);
Value ATCurrentActivityThread(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ATCurrentApplication(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ATCurrentPackageName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ActAttach(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ActGetApplication(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ActGetWindow(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value WindowGetDecorView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value WindowSetContentView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetConfiguration(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ActGetIntent(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ActSetContentView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// 资源布局膨胀面（core/framework_inflate.cc）：setContentView(int)/
// findViewById(int)/addContentView(View, LayoutParams)。
Value ActSetContentViewInt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ActFindViewById(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ActAddContentView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// addContentView 覆盖层登记（首帧管线 traversal 后按窗口原点重排）
void ResetInflateState();
std::vector<VObject*> AddedContentViews(VObject* activity);
// 平台 LayoutInflater 单例（lib VmLayoutInflater；Context 缓存）+ 资源布局钩子
VObject* AllocPlatformLayoutInflater(VMState& state, VObject* context);
Value VmLayoutHostInflate(VMState& state, VObject* self, const Value* args, size_t n,
                          bool* ok);
VObject* AllocResources(VMState& state, const std::string& apk_path, const std::string& package_name);
Value AppGetProcessName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value AppRegisterComponentCallbacks(VMState& state, VObject* self, const Value* args, size_t n,
                                    bool* ok);
Value AppUnregisterComponentCallbacks(VMState& state, VObject* self, const Value* args, size_t n,
                                      bool* ok);
void ResetComponentCallbacks();
Value ActStartActivityImpl(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ActLifecycleCallbacks(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok);
Value ActGetComponentName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ActGetLocalClassName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value IntentFilterInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value IntentFilterAddAction(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value IntentFilterAddCategory(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok);
Value IntentFilterCountActions(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok);
Value IntentFilterGetAction(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value IntentFilterHasAction(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// android.content.ComponentName（framework_app.cc）
Value ComponentNameInit2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ComponentNameInitCtx(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ComponentNameInitCtxClass(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok);
Value ComponentNameGetPackageName(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok);
Value ComponentNameGetClassName(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok);
Value ComponentNameFlattenToString(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok);
Value ComponentNameEquals(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ComponentNameUnflatten(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok);
Value CPAttachInfo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CPGetContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CPSelfCall4(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CPSelfDeleteExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CPSelfInsertExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CPSelfQueryBundle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CPSelfQuerySignal(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CPSelfUpdateExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRBulkInsert(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRCallAuth(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRCallUri(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRDelete(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRDeleteExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRGetType(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRInsert(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRInsertExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRQueryBundle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRQuerySignal(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRUpdate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CRUpdateExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CWAttachBaseContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CWInitBase(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
const std::string& CtxDataRoot();
Value CtxDeleteDatabase(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxDeviceProtectedContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
std::string CtxExternalRoot();
Value CtxGetApplicationContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetApplicationInfo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetBaseContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetCacheDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetCodeCacheDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetContentResolver(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetDataDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetDatabaseNames(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetDatabasePath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetExternalCacheDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetExternalCacheDirs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetExternalFilesDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetExternalFilesDirs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetExternalMediaDirs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetFilesDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxOpenFileOutput(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxOpenFileInput(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetColor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetColor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxObtainStyledAttributes(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok);
Value CtxObtainStyledAttrsRes(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok);
// 资源主题面（framework_app.cc）
void SetAppThemeResId(int32_t res_id);
int32_t GetAppThemeResId();
Value CtxGetTheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetMainLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetStringArgs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
int32_t ContextOwnThemeResId(VObject* self);
Value ContextThemeWrapperInitRes(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok);
Value ContextThemeWrapperInitTheme(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok);
Value ContextThemeWrapperSetTheme(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok);
Value ActSetTheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ThemeSetTo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ThemeApplyStyle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ThemeObtainStyledSelf(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ThemeObtainStyled28(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ThemeObtainStyledSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ThemeResolveAttribute(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetResourcesSelf(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetValue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetDrawable(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResNewTheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriBuildUpon(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetDisplayMetrics(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetDimensionPx(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetDimension(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetColorStateList(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetStringArgs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetQuantityString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetBoolean(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetInteger(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResOpenRawResource(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResResourceName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResResourceEntryName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResResourceTypeName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetNoBackupFilesDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetPackageManager(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetPackageName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetResources(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetAssets(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetSystem(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmAssetsOpen(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);

// java.security.VmDigest：lib sun.security.provider.* 的原生摘要桥（framework_digest.cc）。
Value VmDigestCreate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmDigestUpdate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmDigestDigest(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmDigestReset(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmDigestClose(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);

// java.util.VmLinkedList：lib LinkedList 节点遍历原生桥（framework_collections.cc）。
Value VmLinkedListGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmLinkedListNode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);

// android.graphics.VmFonts：字体/文本度量桥（framework_font.cc）。
Value VmFontsAvailable(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmFontsCreateFromFamily(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok);
Value VmFontsCreateFromFile(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok);
Value VmFontsRestyle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmFontsCreateFromResource(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok);
Value VmFontsMeasureText(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmFontsFontMetrics(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// render 管线文本度量（宿主真字体；false=桥不可用回落网格）
bool HostMeasureTextUtf16(const uint16_t* chars, int32_t len, float text_size,
                          float* out_width);
bool HostFontMetricsInt(float text_size, int32_t out[5]);
Value CtxGetSharedPreferences(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetSharedPreferencesPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetStringArray(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetText(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetTextArray(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxIsDeviceProtected(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxMoveDatabaseFrom(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxMoveSharedPreferencesFrom(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ErrnoExceptionInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
VField* FindInstanceField(VClass* klass, const char* name);
ObjHeader* GetArrRef(VArray* a, int32_t i);
VObject* GetContextApplicationInfo();
std::string GetContextDataDir();
std::string GetContextExternalRoot();
ObjHeader* GetRefField(VObject* o, uint32_t off);
Value HTGetLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HTInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HTQuit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HTQuitSafely(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HTRun(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HTStart(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerDispatchMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerGetLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerHandleMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerHasCallbacks(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerHasMessages(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerCreateAsync(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerCreateAsyncCallback(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerInit0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerInitCallback(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerInitLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerInitLooperCallback(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerObtainMessage0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerObtainMessageWhat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerObtainMessageWhatArg1Arg2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerObtainMessageWhatArg1Arg2Obj(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerObtainMessageWhatArg1Obj(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerObtainMessageWhatObj(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerPost(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerPostAtFrontOfQueue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerPostAtTime(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerPostDelayed(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerRemoveCallbacks(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerRemoveCallbacksAndMessages(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerRemoveMessages(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerSendEmptyMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerSendEmptyMessageAtTime(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerSendMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerSendMessageAtFrontOfQueue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerSendMessageAtTime(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value HandlerSendMessageDelayed(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value InstrCallActivityOnCreate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value InstrCallApplicationOnCreate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value InstrNewApplication(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LogD(VMState& s, VObject* o, const Value* a, size_t n, bool* ok);
Value LogE(VMState& s, VObject* o, const Value* a, size_t n, bool* ok);
Value LogGetStackTraceString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LogI(VMState& s, VObject* o, const Value* a, size_t n, bool* ok);
Value LogV(VMState& s, VObject* o, const Value* a, size_t n, bool* ok);
Value LogW(VMState& s, VObject* o, const Value* a, size_t n, bool* ok);
Value LooperGetMainLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LooperGetQueue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LooperGetThread(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LooperLoop(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LooperMyLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LooperMyQueue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LooperPrepare(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LooperPrepareMain(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LooperQuit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LooperQuitSafely(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MQAddIdleHandler(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MQEnqueueMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MQInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MQNext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MQQuit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MQQuitSafely(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MQRemoveIdleHandler(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
VObject* MakeResourcesObject(VMState& state, const std::string& apk_path, const std::string& package_name);
Value MsgCopyFrom(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgGetCallback(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgGetData(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgGetTarget(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgGetWhen(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgIsAsynchronous(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgObtain0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgObtainHandler(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgObtainHandlerRunnable(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgObtainHandlerWhat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgObtainHandlerWhatArg1Arg2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgObtainHandlerWhatArg1Arg2Obj(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgObtainHandlerWhatObj(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgObtainOrig(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgPeekData(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgRecycle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgSendToTarget(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgSetAsynchronous(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgSetData(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MsgSetTarget(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
VObject* NewLibObject(VMState& state, const char* desc, std::vector<std::string> params, const Value* args, size_t n, bool* ok);
Value OsMyPid(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value OsMyUid(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value OsReadlink(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value PILoadXmlMetaData(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value PMGetProviderInfo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value PMGetResourcesForApplication(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value PMResolveContentProvider(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ProcessMyPid(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ProcessMyTid(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ProcessMyUid(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ProcessMyUserHandle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
bool PumpLooperQueues(VMState& state, std::string* error);
bool PumpQueues(VMState& state, std::string* error, bool all_loopers = false,
                size_t* dispatched_out = nullptr);
bool PumpQueuesRealtime(VMState& state, std::string* error, size_t* dispatched_out = nullptr);
void RegisterFrameworkBuiltins(ClassLinker* linker);
void RegisterProviderInfo(VMState& state, const std::string& authority, VObject* info);
Value ResGetIdentifier(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetStringArray(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ResGetResourcePackageName(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok);
Value ResOpenXmlParser(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value SCElapsedRealtime(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value SCElapsedRealtimeNanos(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value SCSleep(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value SCUptimeMillis(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
void SetArrRef(VArray* a, int32_t i, ObjHeader* v);
void SetContextApplicationInfo(VObject* app_info);
void SetContextDataDir(const std::string& dir);
void SetContextExternalRoot(const std::string& root);
void SetContextPackageManager(VObject* pm);
void SetContextResources(VObject* resources);
void SetFField(VObject* o, uint32_t off, float v);
float GetFField(VObject* o, uint32_t off);
void SetRefField(VObject* o, uint32_t off, ObjHeader* v);
void SetResourceSeam(const ResourceSeam& seam);
Value LLSetOrientation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LLGetOrientation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LLSetGravity(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LLGetGravity(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value TVGetText(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value TVInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value TVSetTextRes(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ButtonInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value EditTextInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// 主线程 peer 单例（builtins_thread.cc）：确定性主线程身份（framework 侧
// Looper 主绑定/身份判定用；不改 theCurrentThread 槽）。
VObject* MainThreadPeer(VMState& state);
// android.app.Fragment/FragmentManager/FragmentTransaction（framework_fragment.cc）
void RegisterFragmentBuiltins(ClassLinker* linker, std::vector<VClass*>* all,
                              VClass* activity);
void ResetFragmentState();
Value FMGetFragmentManagerForActivity(VMState& state, VObject* self, const Value* args, size_t n,
                                      bool* ok);

// android.media 最小面（core/framework_media.cc）
void ResetMediaState();
void SetHostValue(const std::string& key, const std::string& value);
void ClearHostValues();
Value HostGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetSystemService(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value CtxGetSystemServiceClass(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok);
// android.hardware 传感器桥（core/framework_sensor.cc）：
// Context.getSystemService("sensor") 落点 + android.hardware.VmSensors 钩子。
VObject* AllocSensorManager(VMState& state);
Value VmSensorsList(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmSensorsPoll(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// 宿主通知桥（core/framework_notification.cc + core/notification_bridge.cc）：
// android.app.VmNotifications 钩子（宿主 NotificationBridge.java 下发真通知）。
Value VmNotificationsPost(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmNotificationsCancel(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmNotificationsCancelAll(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok);
Value VmNotificationsCreateChannel(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok);
Value VmNotificationsDeleteChannel(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok);
VObject* AllocNotificationManager(VMState& state);
// lib SharedPreferencesImpl 落盘钩子（core/framework_app.cc）：
// android.content.VmPrefsStore.{read,write}
Value VmPrefsRead(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmPrefsWrite(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value AudioManagerGetDevices(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value AudioDeviceGetType(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MediaPlayerInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MediaPlayerNoOp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MediaPlayerSetPreferredDevice(VMState& state, VObject* self, const Value* args, size_t n,
                                    bool* ok);
Value MediaPlayerGetDuration(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MediaPlayerIsPlaying(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VideoViewSetVideoPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VideoViewSeekTo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VideoViewStart(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VideoViewStop(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VideoViewIsPlaying(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// 资源布局面 widget（Switch/ProgressBar/ImageView）：字段缝 intrinsic。
Value SwitchSetChecked(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value SwitchIsChecked(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ProgressBarSetProgress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ProgressBarGetProgress(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ImageViewSetImageResource(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok);
Value TVSetText(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value TVSetTextSize(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriDecode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriEncode1(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriEncode2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriEquals(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriFromParts(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetAuthority(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetEncodedAuthority(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetEncodedFragment(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetEncodedPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetEncodedQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetFragment(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetHost(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetLastPathSegment(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetPathSegments(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetPort(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetQueryParameter(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetQueryParameterNames(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetQueryParameters(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetScheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetSchemeSpecificPart(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriGetUserInfo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriHashCode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriIsAbsolute(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriIsHierarchical(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriIsOpaque(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriIsRelative(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriNormalizeScheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriParse(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UriWithAppendedPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// ---- Uri$Builder（内建内嵌类；实现在 core/framework_app.cc）----
Value UBInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBScheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBAuthority(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBEncodedAuthority(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBEncodedPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBEncodedQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBFragment(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBEncodedFragment(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBClearQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBAppendPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBAppendQueryParameter(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBBuild(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value UBBuildUpon(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGAddView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGAddView2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGRemoveViewAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGGetChildAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGGetChildCount(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGRemoveAllViews(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// ---- View 长尾面（framework 面批量补充轮；实现在 core/framework_view.cc）----
Value LPInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LPInitAttrs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value LPInitCopy(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// 容器 LayoutParams 工厂（generateLayoutParams/generateDefaultLayoutParams）：
// LP 类 = self 描述符 + "$LayoutParams"（AOSP 各容器自有 LP 类型）；无参形给
// WRAP_CONTENT；带 ViewGroup$LayoutParams 源参形复制 width/height/margins。
Value VGGenerateLayoutParams(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok);
Value VGCheckLayoutParams(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value TVGetFilters(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetWindowInsetsController(VMState& state, VObject* self, const Value* args, size_t n,
                                    bool* ok);
Value MSGetMode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MSGetSize(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value MSMakeMeasureSpec(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGGetChildMeasureSpec(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGGetPaddingBottom(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGGetPaddingLeft(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGGetPaddingRight(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGGetPaddingTop(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGMeasureChild(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGRemoveView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VGAttachViewToParent(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok);
Value VGDetachViewFromParent(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok);
Value VGDetachViewFromParentIdx(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok);
Value ViewAnimate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewCombineMeasuredStates(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewFindViewById(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetAlpha(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetBottom(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetId(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetLayoutParams(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetLeft(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetParent(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// 窗口焦点语义（AOSP ViewRootImpl.handleWindowFocusChanged）：
// 首帧后窗口获得焦点，Activity/View 树收到 onWindowFocusChanged(true)，
// View.hasWindowFocus() 随之变真（app 常以此为数据加载闸门）。
void SetGuestWindowFocused(bool focused);
bool GuestWindowFocused();
// View attach/窗口可见性语义（AOSP dispatchAttachedToWindow /
// onWindowVisibilityChanged）：首帧前整树分发；会话内新 addView 的子树立即分发。
void SetGuestViewAttached(bool attached);
bool GuestViewAttached();
bool DispatchViewAttached(VMState& state, VObject* view, bool attached);
Value ViewHasWindowFocus(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewDispatchWindowFocusChanged(VMState& state, VObject* self, const Value* args, size_t n,
                                     bool* ok);
Value ViewGetViewTreeObserver(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok);
// 首帧/每帧 traversal 整树派发（各 View 的 observer 单例）。
void DispatchViewTreeGlobalLayout(VMState& state, VObject* root);
bool DispatchViewTreePreDraw(VMState& state, VObject* root);
// requestLayout/invalidate 脏位（会话帧循环据此触发 traversal）。
void RequestGuestLayout();
bool TakeGuestLayoutRequest();
Value ViewGetViewTreeObserverProbe(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok);
bool DispatchWindowFocusChanged(VMState& state, VObject* view, bool focused);
Value ViewGetRight(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetScrollX(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetScrollY(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetTag0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetTagKeyed(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetTop(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetVisibility(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewInvalidate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewIsLayoutRequested(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok);
void ClearGuestLayoutRequested(VObject* v);
Value ViewPerformClick(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewPost(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewRemoveCallbacks(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewRequestLayout(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewResolveSize(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetAlpha(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetLayoutParams(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetScrollX(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetScrollY(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetTag0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetTagKeyed(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetTranslationX(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetTranslationY(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetVisibility(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetBackground(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewOnScrollChanged(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetPadding(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewPost(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewPostOnAnimation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// View 展示态侧表（selected/activated/rotation/contentDescription）+ ViewGroup 查找
void ResetViewStateRegistries();
Value ViewGroupIndexOfChild(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok);
Value ViewSetSelected(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewIsSelected(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetActivated(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewIsActivated(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetRotation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewGetRotation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value ViewSetContentDescription(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok);
Value ViewGetContentDescription(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok);
Value XmlGetAttributeValue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value XmlGetName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value XmlNext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
extern VObject* g_content_resolver;
extern VObject* g_context_app_info;
extern VObject* g_context_pm;
extern VObject* g_context_resources;
extern std::string g_ctx_data_dir;
extern std::string g_ctx_external_root;
extern std::vector<VObject*> g_loopers;
extern VObject* g_main_thread;
extern VObject* g_msg_pool;
extern int32_t g_msg_pool_size;
extern std::map<std::string, VObject*> g_provider_instances;
extern std::map<std::string, VObject*> g_provider_registry;
extern bool g_pumping;
extern ResourceSeam g_resource_seam;
extern std::map<std::string, VObject*> g_resources_by_apk;
extern std::map<VObject*, VObject*> g_thread_looper;
extern uint64_t g_uptime_anchor_us;

}  // namespace dexvm::runtime
