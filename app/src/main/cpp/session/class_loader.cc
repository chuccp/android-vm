// VM 内建 ClassLoader 模型 —— 会话级 IO 部分。
// LoaderRegistry/LoaderState 实现全在 session/class_loader.h（header-only，
// 保证 core/ 对既有宿主测试脚本零新增链接依赖）。
// 本 TU 提供 dexPath 段读取的完整形态（.dex 直读 + .apk/.jar zip 解析），
// 经 DEXVM_SESSION_IO 编译宏由 core/builtins.cc 引用（见 ReadDexPathBytes）。
#include "session/class_loader.h"

#include "session/session.h"

namespace dexvm::session {

bool ReadDexPathBytes(const std::string& path, std::vector<std::vector<uint8_t>>* out,
                      std::string* error) {
  // session::Session 兼容单 .dex 与 APK/JAR（zip）两形态
  std::string err;
  auto s = Session::Open(path, &err);
  if (s == nullptr) {
    if (error != nullptr) *error = err;
    return false;
  }
  for (const DexBlob& blob : s->dex_blobs()) out->push_back(blob.bytes);
  return !out->empty();
}

void GetLoaderResources(LoaderState* st, const std::string& name, std::vector<ResourceHit>* out) {
  if (st == nullptr || out == nullptr) return;
  // ClassLoader.getResources 语义：双亲链 parent 先、自身后
  if (st->parent != nullptr) GetLoaderResources(st->parent, name, out);
  for (const std::string& zip : st->resource_zips) {
    std::vector<ResourceHit> hits;
    std::string err;
    if (!ReadZipResources(zip, name, &hits, &err)) continue;  // zip 损坏：跳过不致命
    for (auto& h : hits) out->push_back(std::move(h));
  }
}

}  // namespace dexvm::session
