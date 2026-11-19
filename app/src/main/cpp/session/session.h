// APK Session（docs 03 章 §10）：APK/DEX 打开、Manifest、ClassLoader 输入
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "session/class_loader.h"  // ResourceHit（M5 资源面）

namespace dexvm::session {

// 从 APK（zip）或单 DEX 文件提取出的 DEX 字节块
struct DexBlob {
  std::string name;  // "classes.dex" 等
  std::vector<uint8_t> bytes;
};

// <meta-data> 投影条目（AOSP PackageParser.parseMetaData 同型语义：
// android:value 按 AXML typed value 分型；android:resource -> int 资源 id）。
struct MetaEntry {
  enum class Type {
    kString,    // android:value 字符串（putString）
    kInt,       // android:value 整型（putInt；含 TYPE_INT_HEX/颜色位型）
    kBool,      // android:value 布尔（putBoolean）
    kFloat,     // android:value 浮点（putFloat）
    kResource,  // android:resource（putInt 资源 id；AOSP 同口径）
  };
  std::string name;  // android:name
  Type type = Type::kString;
  std::string str;   // kString 值
  int64_t num = 0;   // kInt/kBool/kResource 位值
  float f = 0.0f;    // kFloat 值
};

// manifest 声明的 Provider（docs 19.2 install 顺序：descending initOrder）。
// 值投影面 = AOSP android.content.pm.ProviderInfo 公开字段语义源：
// authorities 原串保留（';' 多值不拆，ProviderInfo.authority 同口径）；
// read/writePermission 缺省回落 android:permission（AOSP PackageParser）。
struct ProviderEntry {
  std::string name;         // android:name（可能为相对名 '.X'）
  int32_t init_order = 0;   // android:initOrder
  // ---- manifest 值投影（binary xml 属性）----
  std::string authorities;       // android:authorities（';' 多值原样）
  std::string process_name;      // android:process（空 = 默认进程）
  std::string permission;        // android:permission
  std::string read_permission;   // android:readPermission（缺省回落 permission）
  std::string write_permission;  // android:writePermission（缺省回落 permission）
  bool exported = false;         // android:exported（缺省 = 含 intent-filter，AOSP）
  bool enabled = true;           // android:enabled（缺省 true，AOSP）
  bool multiprocess = false;     // android:multiprocess
  // android:grantUriPermissions（属性 0x0101001b；AOSP PackageParser.parseProvider）。
  // FileProvider.attachInfo 消费面：false -> SecurityException("Provider must grant
  // uri permissions")。<path-permission> 子元素（grantUriPermissions="read|write"
  // 元组投影 -> ProviderInfo.pathPermissions）后置，见 BuildProviderInfo 注记。
  bool grant_uri_permissions = false;
  std::vector<MetaEntry> meta_data;  // <meta-data> 子元素（声明序）
};

// manifest 声明的 Service（ServiceInfo 值投影面；getServiceInfo 消费）。
struct ServiceEntry {
  std::string name;         // android:name（可能为相对名 '.X'）
  std::string process_name; // android:process（空 = 默认进程）
  std::string permission;   // android:permission
  bool exported = false;    // android:exported（缺省 = 含 intent-filter）
  bool enabled = true;      // android:enabled（缺省 true）
  std::vector<MetaEntry> meta_data;  // <meta-data> 子元素（声明序）
};

// manifest 声明的 Activity（ActivityInfo 值投影面；getActivityInfo 消费——
// androidx 兼容初始化/壳组件查询链）。
struct ActivityEntry {
  std::string name;           // android:name（可能为相对名 '.X'）
  std::string process_name;   // android:process（空 = 默认进程）
  std::string parent_name;    // android:parentActivityName
  std::string target_name;    // android:targetActivity（activity-alias）
  bool exported = false;      // android:exported（缺省 = 含 intent-filter）
  bool enabled = true;        // android:enabled（缺省 true）
  int32_t theme = 0;          // android:theme
  int32_t launch_mode = 0;    // android:launchMode
  int32_t config_changes = 0; // android:configChanges
  std::vector<MetaEntry> meta_data;  // <meta-data> 子元素（声明序）
};

// 最小 Manifest 信息（binary xml 解析；M4：组件明细补全 providers/launcher；
// manifest 值投影：versionCode/versionName、<meta-data>、provider 全字段面）
struct ManifestInfo {
  std::string package_name;
  int32_t version_code = 0;   // android:versionCode（PackageInfo.versionCode 源）
  std::string version_name;   // android:versionName（PackageInfo.versionName 源）
  std::string application_class;  // android:name，无则空
  int32_t min_sdk = 0;
  int32_t target_sdk = 0;
  std::vector<ProviderEntry> providers;   // 声明顺序
  std::vector<ServiceEntry> services;     // 声明顺序（getServiceInfo 投影面）
  std::vector<ActivityEntry> activities;  // 声明顺序（getActivityInfo 投影面）
  std::string launcher_activity;          // MAIN/LAUNCHER activity（android:name）
  std::vector<MetaEntry> application_meta_data;  // <application> 下 <meta-data>
  // <application> 属性投影（ApplicationInfo.flags 源；AOSP parseBaseApplication
  // 同口径：hasCode 缺省 true，其余缺省 false）
  bool debuggable = false;   // android:debuggable
  bool large_heap = false;   // android:largeHeap
  bool has_code = true;      // android:hasCode
  int32_t application_theme = 0;  // <application android:theme> 资源 id
};

class Session {
 public:
  // 打开 APK 或单 .dex；失败返回 nullptr 并写 error
  static std::unique_ptr<Session> Open(const std::string& path, std::string* error);

  // DexFile::Open 借用 DexBlob 字节（不拷贝）；Session 销毁时字节退役保活
  // （对照 LoaderRegistry::retired_bytes_ 防悬挂语义），避免 DexFile 悬挂。
  ~Session();

  // 按 APK 内顺序返回全部 DEX（classes.dex、classes2.dex…；multidex）
  const std::vector<DexBlob>& dex_blobs() const { return dex_blobs_; }

  const ManifestInfo& manifest() const { return manifest_; }
  bool has_manifest() const { return has_manifest_; }

  const std::string& path() const { return path_; }

 private:
  Session() = default;

  bool ParseApk(const std::vector<uint8_t>& zip, std::string* error);
  bool ParseManifest(const std::vector<uint8_t>& axml, std::string* error);

  std::string path_;
  std::vector<DexBlob> dex_blobs_;
  ManifestInfo manifest_;
  bool has_manifest_ = false;
};

// zip entry 按名抽取（M5 ServiceLoader 资源面，docs 14 章 30）：中央目录全量
// 扫描同名 entry（stored/deflate 解压），同名多 entry 全收保序；每条命中带
// url（jar:file:<zip>!/<entry>，url.toString 可解析）与解压字节。非 zip / IO
// 失败返回 false 并写 error；无命中返回 true 且 out 为空（"查无此资源"非错误）。
// 典型用法：META-INF/services/<接口全限定名>（知乎 APM 模块注册表资源面）。
bool ReadZipResources(const std::string& zip_path, const std::string& name,
                      std::vector<ResourceHit>* out, std::string* error);

// ---- resources 面（docs 20 章；首个真实消费方 = FileProvider meta-data）----
// resources.arsc/AXML = zip/字节解析（§31.7 VM native 缝档）：本层只做字节面，
// guest API 面（Resources/XmlResourceParser）在 core/framework_builtins 薄钩。
// 表按 APK 路径进程内缓存（一次解析多查）；config 限定符匹配最小面 = 默认
// config 优先、余取首个命中（设备 config 匹配后置）。

// AXML 资源事件（XmlResourceParser 最小面：标签/属性遍历）。
struct XmlResAttr {
  std::string ns;     // 命名空间 URI（android / app / 空）
  std::string name;
  std::string value;  // rawValue/typed string（无字符串面 = 空）
  // 属性资源 id（AXML resource map：android:text=0x0101014f 等；0 = 无映射）。
  // LayoutInflater 面按 id 归名（编译态 aapt 可能不在 string pool 留名）。
  uint32_t res_id = 0;
  // 类型化值（Res_value）：0x01 reference / 0x03 string / 0x05 dimension /
  // 0x10 int_dec / 0x12 boolean / 0x1c-0x1f color 等；dimension data 为
  // complex（低 4 位单位，高 24 位 8.8 定点）。
  uint8_t value_type = 0;
  uint32_t value_data = 0;
};
struct XmlResEvent {
  enum Type : uint8_t {
    kEndDocument = 1,  // XmlPullParser.END_DOCUMENT
    kStartTag = 2,     // XmlPullParser.START_TAG
    kEndTag = 3,       // XmlPullParser.END_TAG
  };
  Type type = kEndDocument;
  std::string name;               // 标签名（START/END_TAG）
  std::vector<XmlResAttr> attrs;  // START_TAG 属性（声明序）
};

// binary xml（res/xml/* 编译产物）-> 事件流（末尾恒有 kEndDocument）。
// 同 AndroidManifest AXML 语法（ResChunk/string pool/start-end element）。
bool ParseResourceXml(const std::vector<uint8_t>& axml, std::vector<XmlResEvent>* out,
                      std::string* error);

// resId -> APK 内资源文件字节（resources.arsc 查文件引用 -> zip 抽取）。
// FileProvider: Resources.getXml(meta-data 资源 id) -> res/xml/*.xml AXML。
// 返回 false = 查无/IO 失败（error 诊断）；entry_path 为 APK 内路径（诊断面）。
bool ReadResourceFile(const std::string& apk_path, uint32_t res_id, std::string* entry_path,
                      std::vector<uint8_t>* bytes, std::string* error);

// assets/<name> -> 原始字节（AssetManager.open 语义源；stored/deflate 同
// ReadZipResources）。返回 false = 查无/IO 失败（非致命）。
bool ReadAssetFile(const std::string& apk_path, const std::string& name,
                   std::vector<uint8_t>* bytes, std::string* error);

// (type, entry, package) -> resId（Resources.getIdentifier 语义源）；0 = 未命中。
// package_name 空 = 首个包（AOSP defPackage 空语义近似，多包并存后置）。
int32_t LookupResourceId(const std::string& apk_path, const std::string& package_name,
                         const std::string& type, const std::string& entry_name);

// resId -> 字符串资源值（resources.arsc string type：TYPE_STRING -> 全局 pool）。
// Resources.getString/getText 语义源；查无 = false（error 诊断）。
bool LookupResourceString(const std::string& apk_path, uint32_t res_id, std::string* out,
                          std::string* error);

// resId -> 字符串数组（resources.arsc array type：complex bag 逐项 TYPE_STRING）。
// Resources.getStringArray/getTextArray 语义源；查无 = false。
bool LookupResourceStringArray(const std::string& apk_path, uint32_t res_id,
                               std::vector<std::string>* out, std::string* error);

// resId -> 类型化值（Res_value；reference 链解析到终值，最多 16 跳）。
// 终值 TYPE_STRING（0x03）时 *out_string 填全局 string pool 值（data = 池索引）；
// 其余类型（color/dimen/int/bool）由调用方按 Android complex 口径解释 data。
// 未命中 / bag（complex）/ attribute 引用 = false（error 诊断）。
bool LookupResourceValue(const std::string& apk_path, uint32_t res_id, uint8_t* out_type,
                         uint32_t* out_data, std::string* out_string, std::string* error);

// resId -> (package, type, entry) 名（Resources.getResourceName/EntryName/
// TypeName 语义源；type_names/key_names 为 arsc 原值——混淆包如实返回）。
bool LookupResourceName(const std::string& apk_path, uint32_t res_id, std::string* package,
                        std::string* type, std::string* entry, std::string* error);

// style 资源项（ResTable_map：name = attr 资源 id；type/data = Res_value）
struct StyleItem {
  uint32_t name = 0;   // attr 资源 id
  uint8_t type = 0;    // Res_value dataType
  uint32_t data = 0;   // Res_value data
  std::string string;  // TYPE_STRING 时的全局 string pool 值
};

// style 资源 -> 继承 parent + 全部 map 项（Theme.obtainStyledAttributes 语义源；
// 主题解析链按 parent 资源 id 上溯）。
bool LookupResourceStyle(const std::string& apk_path, uint32_t style_res_id, uint32_t* parent,
                         std::vector<StyleItem>* items, std::string* error);

}  // namespace dexvm::session
