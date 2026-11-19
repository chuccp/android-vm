// Guest framework 内建类（M4 阶段 1，docs 10 章 19.1-19.4 / 14 章 29.2）：
// android.content/app/os/view/widget/util 的最小可执行骨架——
// Application/Provider/Activity 生命周期面 + ActivityThread/Instrumentation
// 静态面（壳反射 mInitialApplication 等）+ TextView 构造/setText 路径。
// SharedPreferences 面已按 §31.7 B 档归位 lib（runtime-libs android.content.
// SharedPreferences* 随 runtime.dex 打包）：仅 Context.getSharedPreferences
// 薄钩子转调 lib SharedPreferencesImpl.getInstance(name)。
// 独立于 core/builtins.cc（既有宿主测试按固定源文件列表编译，不注册这些类），
// 生命周期测试经 RegisterFrameworkBuiltins 显式登记。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "session/session.h"  // XmlResEvent（resources 面字节缝）

namespace dexvm::runtime {

class ClassLinker;
struct VMState;
struct VObject;

// 登记 framework 内建类骨架到 ClassLinker（幂等：重复调用无副作用）。
// 须在 RegisterBuiltins 之后调用（依赖 Object/String/异常类）。
void RegisterFrameworkBuiltins(ClassLinker* linker);

// boot dex 装载后的 lib 类 API 补桩第二遍（RegisterFrameworkBuiltins 首遍时
// lib 类尚未 Findable；缺方法否则在真实 app 首现即 NoSuchMethodError）。
void ApplyFrameworkApiStubs(ClassLinker* linker);

// Context 文件/存储面数据根（AOSP app data dir 语义）：设备考语境 = 宿主 app
// files 目录投影（runtime_contract 以 DirName(target_apk) 落点）；宿主测试自设
// 临时根。未显式设置时回落 DEXVM_DATA_DIR 环境变量，再回落 $TMPDIR/dexvm-ctx-data。
// VM 存储面单一（createDeviceProtectedStorageContext 等价 this 的同一口径）。
void SetContextDataDir(const std::string& dir);
std::string GetContextDataDir();

// Context 服务桥系统单例（AOSP：所有 Context 同一系统 PackageManager）：
// 设备考 semface 装配后落点；宿主测试自设。getPackageManager() 未装时抛
// IllegalStateException（不伪造空服务）。ApplicationInfo 同口径。
void SetContextPackageManager(VObject* pm);
void SetContextApplicationInfo(VObject* app_info);

// 外部存储根（AOSP Environment.getExternalStorageDirectory 语义源）：
// getExternalFilesDir(s)/getExternalCacheDir(s)/getExternalMediaDirs 的路径
// 基（<root>/Android/data/<pkg>/files|cache、<root>/Android/media/<pkg>，AOSP
// buildExternalStorageAppDirs 同构）。缺省 "/storage/emulated/0"（真机语义）；
// 设备考 semface 与 Environment 单例同源设置，宿主测试自设临时根。
void SetContextExternalRoot(const std::string& root);
std::string GetContextExternalRoot();
// ApplicationInfo 单例取用（AOSP ComponentInfo.applicationInfo 同源语义）：
// ProviderInfo 装配（framework/lifecycle_driver.cc）挂 applicationInfo 用。
VObject* GetContextApplicationInfo();

// ---- Resources 面（docs 20 章；首个消费方 = FileProvider meta-data）----
// 字节缝（§31.7：zip/字节解析 = VM native 缝档；session::ReadResourceFile/
// LookupResourceId/ParseResourceXml 挂入）。未挂 = getIdentifier 0、getXml
// 抛 Resources.NotFoundException（既有测试面不变）。
struct ResourceSeam {
  bool (*open_resource_file)(const std::string& apk_path, uint32_t res_id,
                             std::string* entry_path, std::vector<uint8_t>* bytes,
                             std::string* error);
  int32_t (*lookup_resource_id)(const std::string& apk_path, const std::string& package_name,
                                const std::string& type, const std::string& entry_name);
  bool (*parse_xml_events)(const std::vector<uint8_t>& axml,
                           std::vector<session::XmlResEvent>* out, std::string* error);
  // 值面（Resources.getString/getStringArray 语义源；未挂 = 查无语义不变）
  bool (*lookup_string)(const std::string& apk_path, uint32_t res_id, std::string* out,
                        std::string* error);
  bool (*lookup_string_array)(const std::string& apk_path, uint32_t res_id,
                              std::vector<std::string>* out, std::string* error);
  // 类型化值（Res_value 终值；LayoutInflater 面 @color/@dimen/@string 解析源）
  bool (*lookup_value)(const std::string& apk_path, uint32_t res_id, uint8_t* out_type,
                       uint32_t* out_data, std::string* out_string, std::string* error);
  // style 资源（Theme.obtainStyledAttributes 语义源；parent 链 + map 项）
  bool (*lookup_style)(const std::string& apk_path, uint32_t style_res_id, uint32_t* parent,
                       std::vector<session::StyleItem>* items, std::string* error);
  // resId -> (package, type, entry) 名（getResourceName 族语义源）
  bool (*lookup_name)(const std::string& apk_path, uint32_t res_id, std::string* package,
                      std::string* type, std::string* entry, std::string* error);
  // 资产缝：assets/<name> 原始字节（AssetManager.open 语义源；查无返回 false）。
  bool (*open_asset_file)(const std::string& apk_path, const std::string& name,
                          std::vector<uint8_t>* bytes, std::string* error);
};
void SetResourceSeam(const ResourceSeam& seam);
// 当前资源缝（LayoutInflater 面 @color/@dimen/@string 解析取用；可能为空表）。
const ResourceSeam* GetResourceSeam();

// Resources 实例装配（AOSP Resources(app)：一张 APK 资源表）：pm.
// getResourcesForApplication 语义落点。按 apk_path 进程内缓存单例（GC 钉住，
// 同 Context 服务桥单例口径）。
VObject* AllocResources(VMState& state, const std::string& apk_path,
                        const std::string& package_name);

// Drawable 资源面：Resources.getDrawable 返回的 drawable（含 XML vector 占位）
// 在 VM 侧登记 resource id；桥接快照对 ImageView.mDrawable 反查该 id，宿主用
// guest APK 的真实资源表装载绘制（图标/Drawable 物化的通用通路）。
// 登记时 Pin（侧表键 = GC 根），RegisterRenderPipeline 每 VM 清表。
void RegisterGuestDrawableResource(VMState& state, VObject* drawable, uint32_t res_id,
                                   bool include_state_refs);
uint32_t ResolveGuestDrawableResource(VObject* drawable);
void ResetGuestDrawableResources();

// Context.getResources() 单例落点（AOSP app Resources；semface/宿主测试装配）。
void SetContextResources(VObject* resources);

// Activity.startActivity 请求队列（AOSP AMS 调度等价面）：存储目标组件 +
// Intent 引用（请求期 GC 钉住），LifecycleDriver 在阶段间排空并驱动目标
// Activity 生命周期（onPause/onStop 旧 -> attach/onCreate/onStart/onResume 新）。
void SetPendingActivityLaunch(const std::string& pkg, const std::string& cls, VMState& state,
                              VObject* intent);
bool TakePendingActivityLaunch(std::string* pkg, std::string* cls, ObjHeader** intent);
void ClearPendingActivityLaunch();

// 宿主桥接值面（Host.get 语义源）：宿主推入 telephony.* 等系统服务真值快照；
// guest lib 类经 com.dexvm.runtime.Host.get(key) 读取（桥接优先）。
void SetHostValue(const std::string& key, const std::string& value);
void ClearHostValues();

// app 主题资源 id（manifest <application android:theme> / launcher Activity theme
// 投影；Activity.getTheme/Context.getTheme 的 Theme 对象按此解析属性表）。
void SetAppThemeResId(int32_t res_id);
int32_t GetAppThemeResId();

// AOSP starting window 面（WindowManager 首帧前预览层）：解析 activity 主题的
// android:windowBackground → 背景色 + 居中 bitmap（layer-list 常见形制）。
// 主题选择由调用方按 Android 口径给出（activity theme 优先、app theme 兜底）。
bool ResolveStartingWindowTheme(const std::string& apk, int32_t theme_res_id,
                                uint32_t* out_color, std::vector<uint8_t>* out_bitmap,
                                int32_t* out_gravity);

// PackageManager.resolveContentProvider 数据源（manifest ProviderEntry ->
// ProviderInfo 实例，authority 键注册表；AOSP PMS 查询面 VM 等价）。实例经
// GC 钉住（PMS 自身即根的等价语义）。重复键覆写。
void RegisterProviderInfo(VMState& state, const std::string& authority, VObject* info);

// M4.3 消息循环子系统（docs 04 章 14.10 / 10 章 19.1）：Looper/Handler/
// MessageQueue/Message/HandlerThread/Process/SystemClock 内建面随
// RegisterFrameworkBuiltins 一并登记。线程模型：执行=单线程+协程（泵驱动），
// Looper 按线程身份 key 化（Thread*->Looper* 查找；Handler 默认绑创建线程的
// Looper；HandlerThread=新 GuestTask+自己的 Looper）——动态线程（GuestTask
// 1:1）就绪，无全局单例队列。消息泵（虚拟时间推进）：按 when 升序出队全部
// 已入队消息并 dispatchMessage（延迟消息在 pump 时出队，不阻塞不占 OS 线程
// ——coroutine wait/wake 接线后置）。返回 false = 消息 dispatch 抛出 guest
// 异常（error 为 "descriptor: message" 诊断，pending exception 已取走）。
bool PumpLooperQueues(VMState& state, std::string* error);
// 同上并回传本轮 dispatch 条数（会话 tick 诊断用）
bool PumpLooperQueuesCount(VMState& state, std::string* error, size_t* dispatched);
// 实时语义泵（常驻会话）：只派发已到期消息，postDelayed 按真实时间等待
bool PumpLooperQueuesRealtime(VMState& state, std::string* error, size_t* dispatched);
// IdleHandler 执行轮数（会话诊断）
size_t IdleHandlerRuns();

// 全 Looper 队列排空（宿主测试专用：跨线程"虚拟时间推进"；生产路径不得用
// ——AOSP 消息只在所属线程派发，全量泵会让后台工作挤占主线程）。
bool PumpAllLooperQueues(VMState& state, std::string* error);

// AOSP View.resolveLayoutParams 等价面：LP 的 START/END 锚点按布局方向落
// resolved* 槽（ConstraintLayout.LayoutParams 覆写 resolveLayoutDirection；
// inflater/addView 直写 mLayoutParams 的路径必须补此调用）。direction=0 即 LTR。
void ResolveLayoutParamsDirection(VMState& state, ObjHeader* lp, int32_t direction);

}  // namespace dexvm::runtime
