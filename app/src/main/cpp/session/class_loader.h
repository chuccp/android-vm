// VM 内建 ClassLoader 模型（docs 03 章 11.1 / 12 章 22.1 / 14 章 28.1/28.2）。
// 对照（只读）：
//   class_linker.cc:3078-3170   FindClassInBaseDexClassLoader 委派顺序
//   dalvik_system_DexFile.cc    cookie / defineClassNative / getClassNameList / close
//   dalvik_system_BaseDexClassLoader.cc  DexPathList.Element = DexFile[] + nativeLibraryDir
//
// 实现全部 inline（header-only）：既有宿主测试脚本按固定源文件列表编译 core/*，
// loader 模型不得给 core/ 引入额外链接单元；会话级 IO（apk/jar zip 解析）在
// session/class_loader.cc（需 DEXVM_SESSION_IO + session.cc + zlib）。
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/vm_types.h"

namespace dexvm::session {

// "Lfoo/Bar;" <-> "foo.Bar"（descriptor ↔ dot 名，dalvik_system_DexFile.cc
// DotToDescriptor / DescriptorToDot 等价）
inline std::string DescriptorToDot(std::string_view descriptor) {
  if (descriptor.size() >= 2 && descriptor.front() == 'L' && descriptor.back() == ';') {
    std::string dot(descriptor.substr(1, descriptor.size() - 2));
    std::replace(dot.begin(), dot.end(), '/', '.');
    return dot;
  }
  return std::string(descriptor);
}

inline std::string DotToDescriptor(std::string_view dot) {
  if (dot.empty()) return std::string(dot);
  if (dot.front() == 'L' || dot.front() == '[') return std::string(dot);  // 已是 descriptor
  std::string desc = "L";
  for (char c : dot) desc.push_back(c == '.' ? '/' : c);
  desc.push_back(';');
  return desc;
}

// DexPathList.Element 的等价物：一个 loader 的 DexFile[]（有序）+ 委派链 + 库路径。
// 委派顺序（ART 3078-3170）：parent 递归 → shared libraries（后置）→ 自身 DexFile
// 列表顺序 → shared-after（后置）。DelegateLastClassLoader 反序后置不实现。
struct LoaderState {
  LoaderState* parent = nullptr;              // 双亲（nullptr = boot 之上）
  std::vector<dex::DexFile*> dex_files;       // 自身 DexFile 列表（DexPathList 顺序）
  std::string native_library_path;            // ':' 分隔（libPath）
  runtime::VObject* loader_object = nullptr;  // 对应 java.lang.ClassLoader 对象
  bool is_boot = false;                       // boot class loader 落点
  // 资源源（M5 ServiceLoader/ClassLoader.getResources 面）：dexPath 里的
  // .apk/.jar zip 路径（DexPathList 顺序）。boot runtime.dex 为裸 .dex，无资源。
  std::vector<std::string> resource_zips;
};

// 资源命中（url.toString 可解析：jar:file:<zip>!/<entry>）
struct ResourceHit {
  std::string url;
  std::vector<uint8_t> bytes;
};

// 沿双亲链聚合资源（ClassLoader.getResources 语义：parent 先、自身后；
// 自身多个 zip 按 resource_zips 序，zip 内同名多 entry 保序）。
// zip 抽取（stored/deflate）在 session/class_loader.cc（DEXVM_SESSION_IO 构建）；
// 无命中返回空（"查无此资源"非错误，zip 损坏跳过该 zip 不致命）。
void GetLoaderResources(LoaderState* st, const std::string& name,
                        std::vector<ResourceHit>* out);

// VM 侧 DexFile 登记表：cookie = 不透明 ID（docs 14 章 28.2；ART cookie 为
// vector<DexFile*>，M2 收敛为登记表句柄，壳反射改写留 M3）。
// 所有 DexFile 字节与对象由登记表统一持有；loader/cookie 只持指针。
class LoaderRegistry {
 public:
  // Dex 再生成回调：同路径二次 open 新 DEX 版本时让 ClassLinker 退役旧
  // DexFile（DexCache + 类索引重建，docs 03 章 10.2）。
  using DexInvalidator = std::function<void(dex::DexFile*)>;

  LoaderRegistry() {
    boot_.is_boot = true;
    boot_.parent = nullptr;
    default_app_.parent = &boot_;
  }
  ~LoaderRegistry() = default;

  void set_invalidator(DexInvalidator fn) { invalidator_ = std::move(fn); }

  // ---- LoaderState 生命周期 ----
  LoaderState* boot() { return &boot_; }
  LoaderState* default_app() { return &default_app_; }
  // 建新 loader 状态；parent==nullptr 时挂 boot（ART：null parent = system）。
  LoaderState* CreateLoader(LoaderState* parent, std::string native_library_path) {
    auto st = std::make_unique<LoaderState>();
    st->parent = parent != nullptr ? parent : &boot_;
    st->native_library_path = std::move(native_library_path);
    LoaderState* raw = st.get();
    states_.push_back(std::move(st));
    return raw;
  }
  // loader 对象 ↔ 状态绑定（dalvik.system.BaseDexClassLoader 构造 intrinsic 用）
  void BindObject(runtime::VObject* obj, LoaderState* state) {
    if (obj == nullptr || state == nullptr) return;
    state->loader_object = obj;
    object_to_state_[obj] = state;
  }
  LoaderState* Lookup(const runtime::VObject* obj) const {
    auto it = object_to_state_.find(obj);
    return it == object_to_state_.end() ? nullptr : it->second;
  }
  // 未绑定对象补一个空状态（custom ClassLoader：dex 空、parent=parent_state）
  LoaderState* LookupOrCreate(runtime::VObject* obj, LoaderState* parent_state) {
    if (obj == nullptr) return &boot_;
    if (LoaderState* st = Lookup(obj)) return st;
    LoaderState* st = CreateLoader(parent_state, "");
    BindObject(obj, st);
    return st;
  }

  // ---- Dex 登记 / cookie ----
  struct DexEntry {
    std::string location;            // 文件路径或 <memory:N>
    uint32_t generation = 1;         // DEX generation（docs 03 章 10.2）
    uint64_t checksum = 0;
    std::vector<std::vector<uint8_t>> bytes;  // 各 DEX 字节（multidex）
    std::vector<std::unique_ptr<dex::DexFile>> dex_files;
    bool closed = false;
  };

  // 打开一组 DEX 字节并登记。同 location 且 checksum 相同：幂等复用既有登记
  // （返回原 cookie）。checksum 变化（加固器再生成）：generation+1，旧
  // DexFile 逐个经 invalidator_ 退役（DexCache/类索引重建），loader 列表中
  // 的旧指针按位替换为新 DexFile。失败返回 0 并写 error。
  uint64_t OpenDexSet(const std::string& location,
                      std::vector<std::vector<uint8_t>> dex_bytes, std::string* error) {
    if (dex_bytes.empty()) {
      if (error != nullptr) *error = "no dex bytes for: " + location;
      return 0;
    }
    uint64_t checksum = Fnv1a(dex_bytes);

    DexEntry* existing = FindByLocation(location);
    if (existing != nullptr && existing->checksum == checksum && !existing->closed) {
      return location_to_cookie_[location];  // 幂等：同版本复用
    }

    auto entry = std::make_unique<DexEntry>();
    entry->location = location;
    entry->checksum = checksum;
    entry->bytes = std::move(dex_bytes);
    for (auto& bytes : entry->bytes) {
      std::string err;
      auto dex = dex::DexFile::Open(bytes.data(), bytes.size(), &err);
      if (dex == nullptr) {
        if (error != nullptr) *error = "dex open failed (" + location + "): " + err;
        return 0;
      }
      entry->dex_files.push_back(std::move(dex));
    }

    uint64_t cookie = next_cookie_++;

    if (existing != nullptr) {
      // 加固器 DEX 再生成（docs 03 章 10.2）：同路径新版本 → generation+1，
      // 旧 DexFile/DexCache/类索引重建。旧字节与旧 DexFile 退役保活（不释放，
      // 防既有 VClass/VMethod 悬挂——非移动堆 VM 生命周期简化）。
      entry->generation = existing->generation + 1;
      std::vector<dex::DexFile*> old_dex;
      for (auto& d : existing->dex_files) old_dex.push_back(d.get());

      // loader 列表按位替换旧指针（multidex 位置对应）
      std::unordered_map<dex::DexFile*, dex::DexFile*> remap;
      size_t n = std::min(old_dex.size(), entry->dex_files.size());
      for (size_t i = 0; i < n; i++) remap[old_dex[i]] = entry->dex_files[i].get();
      for (LoaderState* st : {&boot_, &default_app_}) {
        for (dex::DexFile*& slot : st->dex_files) {
          auto it = remap.find(slot);
          if (it != remap.end()) slot = it->second;
        }
      }
      for (auto& stp : states_) {
        for (dex::DexFile*& slot : stp->dex_files) {
          auto it = remap.find(slot);
          if (it != remap.end()) slot = it->second;
        }
      }
      // 旧 DexFile：退役（ClassLinker 侧清 DexCache + 退类索引）后保活
      for (dex::DexFile* d : old_dex) {
        if (invalidator_) invalidator_(d);
        dex_to_state_.erase(d);
        dex_to_cookie_.erase(d);
      }
      for (auto& d : existing->dex_files) retired_dex_.push_back(std::move(d));
      for (auto& b : existing->bytes) retired_bytes_.push_back(std::move(b));

      uint64_t old_cookie = location_to_cookie_[location];
      entries_.erase(old_cookie);
      location_to_cookie_[location] = cookie;
    } else {
      entry->generation = 1;
      location_to_cookie_[location] = cookie;
    }

    // dex -> loader 归属回填（新 dex 对应旧 dex 的归属；否则待 AttachDex）
    for (LoaderState* st : {&boot_, &default_app_}) {
      for (dex::DexFile* d : st->dex_files) {
        for (auto& nd : entry->dex_files) {
          if (nd.get() == d) dex_to_state_[d] = st;
        }
      }
    }
    for (auto& stp : states_) {
      for (dex::DexFile* d : stp->dex_files) {
        for (auto& nd : entry->dex_files) {
          if (nd.get() == d) dex_to_state_[d] = stp.get();
        }
      }
    }
    for (auto& nd : entry->dex_files) dex_to_cookie_[nd.get()] = cookie;

    entries_[cookie] = std::move(entry);
    return cookie;
  }

  DexEntry* GetEntry(uint64_t cookie) {
    auto it = entries_.find(cookie);
    return it == entries_.end() ? nullptr : it->second.get();
  }
  const DexEntry* GetEntry(uint64_t cookie) const {
    auto it = entries_.find(cookie);
    return it == entries_.end() ? nullptr : it->second.get();
  }
  std::vector<dex::DexFile*> GetDexFiles(uint64_t cookie) const {
    std::vector<dex::DexFile*> out;
    const DexEntry* e = GetEntry(cookie);
    if (e == nullptr) return out;
    for (const auto& d : e->dex_files) out.push_back(d.get());
    return out;
  }

  // closeDexFile 语义（dalvik_system_DexFile.cc:424-469）：清 DexCache 后
  // 【未注册】（无 loader 引用）才移除；全删净返回 true（unmap 等价）。
  bool CloseCookie(uint64_t cookie) {
    DexEntry* e = GetEntry(cookie);
    if (e == nullptr) return false;  // 已关/未知 cookie
    for (const auto& d : e->dex_files) {
      if (IsRegistered(d.get())) return false;
    }
    for (auto& d : e->dex_files) {
      if (invalidator_) invalidator_(d.get());  // 清 DexCache/类索引
      dex_to_state_.erase(d.get());
      dex_to_cookie_.erase(d.get());
      retired_dex_.push_back(std::move(d));  // 退役保活，防悬挂
    }
    for (auto& b : e->bytes) retired_bytes_.push_back(std::move(b));
    location_to_cookie_.erase(e->location);
    entries_.erase(cookie);
    return true;  // 全删净
  }

  // ---- dex ↔ loader 关联（RegisterDexFile / InsertDexFileInToClassLoader）----
  void SetDexLoader(dex::DexFile* dex, LoaderState* state) {
    if (dex == nullptr) return;
    dex_to_state_[dex] = state;
  }
  LoaderState* GetDexLoader(dex::DexFile* dex) const {
    auto it = dex_to_state_.find(dex);
    return it == dex_to_state_.end() ? nullptr : it->second;
  }
  void AttachDex(LoaderState* state, dex::DexFile* dex) {
    if (state == nullptr || dex == nullptr) return;
    auto& list = state->dex_files;
    if (std::find(list.begin(), list.end(), dex) == list.end()) list.push_back(dex);
    SetDexLoader(dex, state);
  }
  void DetachDex(dex::DexFile* dex) {
    if (dex == nullptr) return;
    for (LoaderState* st : {&boot_, &default_app_}) {
      st->dex_files.erase(std::remove(st->dex_files.begin(), st->dex_files.end(), dex),
                          st->dex_files.end());
    }
    for (auto& stp : states_) {
      auto& list = stp->dex_files;
      list.erase(std::remove(list.begin(), list.end(), dex), list.end());
    }
    dex_to_state_.erase(dex);
  }
  bool IsRegistered(dex::DexFile* dex) const {
    if (dex == nullptr) return false;
    auto it = dex_to_state_.find(dex);
    return it != dex_to_state_.end() && it->second != nullptr;
  }

  // 在 state 自身 DexFile 列表按顺序找 ClassDef（返回 class_def_idx；-1 未命中）
  int FindClassDef(LoaderState* state, std::string_view descriptor,
                   dex::DexFile** out_dex) const {
    if (state == nullptr) return -1;
    for (dex::DexFile* dex : state->dex_files) {
      if (dex == nullptr) continue;
      int idx = dex->FindClassDefIndexByDescriptor(descriptor);
      if (idx >= 0) {
        if (out_dex != nullptr) *out_dex = dex;
        return idx;
      }
    }
    return -1;
  }

  // 双亲委派链检索（parent 递归 → 自身列表；shared libraries 后置）。
  // 命中写 *out_dex / *out_idx / *out_state（定义方状态，用于绑定 class_loader）。
  bool FindInChain(LoaderState* state, std::string_view descriptor, dex::DexFile** out_dex,
                   uint32_t* out_idx, LoaderState** out_state) {
    if (state == nullptr) return false;
    if (!state->is_boot) {
      // 双亲委派：parent 递归（ART class_linker.cc:3078-3170 首步）
      LoaderState* p = state->parent != nullptr ? state->parent : &boot_;
      if (FindInChain(p, descriptor, out_dex, out_idx, out_state)) return true;
      // shared libraries → 后置（M2 无 shared libraries 模型）
    }
    // 自身 DexFile 列表顺序（DexPathList.Element.dexFiles 先到先得）
    dex::DexFile* dex = nullptr;
    int idx = FindClassDef(state, descriptor, &dex);
    if (idx >= 0) {
      if (out_dex != nullptr) *out_dex = dex;
      if (out_idx != nullptr) *out_idx = static_cast<uint32_t>(idx);
      if (out_state != nullptr) *out_state = state;
      return true;
    }
    // shared-after → 后置
    return false;
  }

  size_t loader_count() const { return object_to_state_.size(); }

 private:
  static uint64_t Fnv1a(const std::vector<std::vector<uint8_t>>& parts) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint8_t b) {
      h ^= b;
      h *= 1099511628211ull;
    };
    uint64_t n = parts.size();
    for (int i = 7; i >= 0; i--) mix(static_cast<uint8_t>(n >> (8 * i)));
    for (const auto& p : parts) {
      for (uint8_t byte : p) mix(byte);
      mix(0xff);  // 分段分隔
    }
    return h;
  }

  DexEntry* FindByLocation(const std::string& location) {
    auto it = location_to_cookie_.find(location);
    return it == location_to_cookie_.end() ? nullptr : GetEntry(it->second);
  }

  DexInvalidator invalidator_;
  LoaderState boot_;
  LoaderState default_app_;
  std::vector<std::unique_ptr<LoaderState>> states_;
  std::unordered_map<const runtime::VObject*, LoaderState*> object_to_state_;
  std::unordered_map<uint64_t, std::unique_ptr<DexEntry>> entries_;  // cookie -> entry
  std::unordered_map<std::string, uint64_t> location_to_cookie_;
  std::unordered_map<dex::DexFile*, LoaderState*> dex_to_state_;
  std::unordered_map<const dex::DexFile*, uint64_t> dex_to_cookie_;
  // 退役保活（再生成/close 后旧字节与旧 DexFile 不释放，防 VClass/VMethod 悬挂）
  std::vector<std::unique_ptr<dex::DexFile>> retired_dex_;
  std::vector<std::vector<uint8_t>> retired_bytes_;
  uint64_t next_cookie_ = 1;
};

// dexPath 段读取（.dex 直读；.apk/.jar zip 走 session::Session，需 DEXVM_SESSION_IO
// 构建：session/class_loader.cc + session/session.cc + zlib）。非该构建下仅支持
// 单 .dex（宿主旧脚本编译面）。
bool ReadDexPathBytes(const std::string& path, std::vector<std::vector<uint8_t>>* out,
                      std::string* error);

}  // namespace dexvm::session
