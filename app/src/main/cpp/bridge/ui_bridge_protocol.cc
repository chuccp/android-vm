// UI 桥接协议实现（P1）：序列化/revision 管理/guest View 树快照导出 +
// 缝A draw 命令流收集。docs 09 章 18.3 桥轨形制。
// 通用 framework 桥接形态：HostHandle/PropValue 泛型（View 只是 kind=kView
// 第一类，后续 Context 服务/Window/资源复用同骨架）。
#include "bridge/ui_bridge_protocol.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "bridge/ui_bridge_input.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/framework_dialog.h"
#include "core/interpreter.h"
#include "session/class_loader.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace dexvm::bridge {

namespace {

using dexvm::runtime::VMState;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::VArray;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;

// ---- 字段槽读取（8 字节槽；同 core/framework_view.cc 约定）----

VField* FindField(VClass* klass, const char* name) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == name) return &f;
    }
  }
  return nullptr;
}

int32_t GetIField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  return static_cast<int32_t>(slot);
}

float GetFField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  float v = 0;
  std::memcpy(&v, &slot, sizeof(v));
  return v;
}

ObjHeader* GetRefField(VObject* o, uint32_t off) {
  ObjHeader* v = nullptr;
  std::memcpy(&v, o->field_data() + off, sizeof(v));
  return v;
}

// 容器布局语义（宿主 fallback measure/layout 的类事实）：按真实父类链判定，
// 不用类名后缀猜（FeedSearchView 等自定义容器名不含 Frame/Linear）。
int32_t LayoutKindOf(VObject* view) {
  if (view == nullptr) return 0;
  for (VClass* k = view->klass; k != nullptr; k = k->super_class) {
    if (k->descriptor == "Landroid/widget/LinearLayout;") return 1;
    // appcompat 线性容器（ZHLinearLayout/ZHMainTabContainer 的父类）：
    // 不是 android.widget.LinearLayout 子类，但布局语义相同。
    if (k->descriptor == "Landroidx/appcompat/widget/LinearLayoutCompat;") return 1;
    if (k->descriptor == "Landroid/widget/FrameLayout;") return 2;
    if (k->descriptor == "Landroid/widget/RelativeLayout;") return 3;
    if (k->descriptor == "Landroid/widget/ScrollView;" ||
        k->descriptor == "Landroid/widget/HorizontalScrollView;") {
      return 4;
    }
  }
  return 0;
}

int32_t ViewI(VObject* v, const char* name) {
  VField* f = FindField(v->klass, name);
  return f != nullptr ? GetIField(v, f->offset) : 0;
}

float ViewF(VObject* v, const char* name) {
  VField* f = FindField(v->klass, name);
  return f != nullptr ? GetFField(v, f->offset) : 0.0f;
}

ObjHeader* ViewRef(VObject* v, const char* name) {
  VField* f = FindField(v->klass, name);
  return f != nullptr ? GetRefField(v, f->offset) : nullptr;
}

// 引用字段若为 guest String 才当字符串读（CharSequence/Spannable 等跳过）。
std::string Utf16ToUtf8(const uint16_t* chars, int32_t len);  // 定义于本 TU 后段

VString* AsGuestString(ObjHeader* ref) {
  if (ref == nullptr || ref->klass == nullptr) return nullptr;
  if (ref->klass->descriptor != "Ljava/lang/String;") return nullptr;
  return reinterpret_cast<VString*>(ref);
}

// String[] 字段 -> '\n' 连接（菜单标题/建议项镜像）。
std::string JoinStringArray(VArray* array) {
  std::string joined;
  if (array == nullptr) return joined;
  for (int32_t i = 0; i < array->length; i++) {
    ObjHeader* sref = nullptr;
    std::memcpy(&sref, array->data() + static_cast<size_t>(i) * 8, sizeof(sref));
    if (i > 0) joined.push_back('\n');
    if (VString* s = AsGuestString(sref)) {
      joined += Utf16ToUtf8(s->chars(), s->length);
    }
  }
  return joined;
}

// ---- UTF-16 <-> UTF-8 ----

std::string Utf16ToUtf8(const uint16_t* chars, int32_t len) {
  std::string out;
  if (chars == nullptr || len <= 0) return out;
  out.reserve(static_cast<size_t>(len));
  for (int32_t i = 0; i < len; i++) {
    uint32_t cp = chars[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len && chars[i + 1] >= 0xDC00 &&
        chars[i + 1] <= 0xDFFF) {
      cp = 0x10000u + ((cp - 0xD800u) << 10) + (chars[i + 1] - 0xDC00u);
      i++;
    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
      cp = '?';  // 孤立代理项（实践不出现）
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

std::u16string Utf8ToUtf16(const std::string& s) {
  std::u16string out;
  size_t i = 0;
  while (i < s.size()) {
    uint32_t cp = 0;
    unsigned char c = static_cast<unsigned char>(s[i]);
    int extra = 0;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      extra = 3;
    } else {
      i++;
      continue;
    }
    i++;
    for (int k = 0; k < extra && i < s.size(); k++, i++) {
      cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
  }
  return out;
}

// ---- 线格式读写（小端）----

void PutU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void PutU16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v));
  b.push_back(static_cast<uint8_t>(v >> 8));
}
void PutU32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void PutI32(std::vector<uint8_t>& b, int32_t v) { PutU32(b, static_cast<uint32_t>(v)); }
void PutF32(std::vector<uint8_t>& b, float v) {
  uint32_t u = 0;
  std::memcpy(&u, &v, sizeof(u));
  PutU32(b, u);
}
// 字符串线形：u16 字节长 + UTF-16LE（与 Java UTF_16LE 精确互转）。
void PutStr16(std::vector<uint8_t>& b, const std::u16string& s) {
  PutU16(b, static_cast<uint16_t>(s.size() * 2));
  for (char16_t c : s) PutU16(b, static_cast<uint16_t>(c));
}

struct Reader {
  const uint8_t* p;
  size_t n;
  size_t off = 0;
  bool ok = true;
  bool Need(size_t k) {
    if (off + k > n) {
      ok = false;
      return false;
    }
    return true;
  }
  uint8_t U8() {
    if (!Need(1)) return 0;
    return p[off++];
  }
  uint16_t U16() {
    if (!Need(2)) return 0;
    uint16_t v = static_cast<uint16_t>(p[off] | (p[off + 1] << 8));
    off += 2;
    return v;
  }
  uint32_t U32() {
    if (!Need(4)) return 0;
    uint32_t v = static_cast<uint32_t>(p[off]) | (static_cast<uint32_t>(p[off + 1]) << 8) |
                 (static_cast<uint32_t>(p[off + 2]) << 16) |
                 (static_cast<uint32_t>(p[off + 3]) << 24);
    off += 4;
    return v;
  }
  int32_t I32() { return static_cast<int32_t>(U32()); }
  float F32() {
    uint32_t u = U32();
    float f = 0;
    std::memcpy(&f, &u, sizeof(f));
    return f;
  }
  std::u16string Str16() {
    uint16_t bytes = U16();
    std::u16string s;
    if (!Need(bytes)) return s;
    s.reserve(bytes / 2);
    for (uint16_t i = 0; i + 1 < bytes; i += 2) {
      s.push_back(static_cast<char16_t>(p[off + i] | (p[off + i + 1] << 8)));
    }
    off += bytes;
    return s;
  }
};

std::string Str16ToUtf8(Reader& r) {
  std::u16string s = r.Str16();
  return Utf16ToUtf8(reinterpret_cast<const uint16_t*>(s.data()),
                     static_cast<int32_t>(s.size()));
}

void PutProp(std::vector<uint8_t>& b, const Prop& pr) {
  PutU16(b, pr.key);
  PutU8(b, static_cast<uint8_t>(pr.value.type));
  PutU8(b, 0);
  switch (pr.value.type) {
    case PropType::kInt:
    case PropType::kBool:
      PutI32(b, pr.value.i);
      break;
    case PropType::kFloat:
      PutF32(b, pr.value.f);
      break;
    case PropType::kString:
      PutStr16(b, Utf8ToUtf16(pr.value.s));
      break;
    case PropType::kRect:
      for (int i = 0; i < 4; i++) PutI32(b, pr.value.rect[i]);
      break;
    case PropType::kPoint:
      PutI32(b, pr.value.rect[0]);
      PutI32(b, pr.value.rect[1]);
      break;
    case PropType::kHandle:
      PutU32(b, pr.value.handle.object_id);
      PutU8(b, static_cast<uint8_t>(pr.value.handle.kind));
      PutU8(b, 0);
      PutU16(b, 0);
      break;
    case PropType::kBytes:
      PutU32(b, static_cast<uint32_t>(pr.value.bytes.size()));
      b.insert(b.end(), pr.value.bytes.begin(), pr.value.bytes.end());
      break;
  }
}

Prop GetProp(Reader& r) {
  Prop pr;
  pr.key = r.U16();
  pr.value.type = static_cast<PropType>(r.U8());
  r.U8();
  switch (pr.value.type) {
    case PropType::kInt:
    case PropType::kBool:
      pr.value.i = r.I32();
      break;
    case PropType::kFloat:
      pr.value.f = r.F32();
      break;
    case PropType::kString:
      pr.value.s = Str16ToUtf8(r);
      break;
    case PropType::kRect:
      for (int i = 0; i < 4; i++) pr.value.rect[i] = r.I32();
      break;
    case PropType::kPoint:
      pr.value.rect[0] = r.I32();
      pr.value.rect[1] = r.I32();
      break;
    case PropType::kHandle:
      pr.value.handle.object_id = r.U32();
      pr.value.handle.kind = static_cast<HostObjectKind>(r.U8());
      r.U8();
      r.U16();
      break;
    case PropType::kBytes: {
      uint32_t len = r.U32();
      if (!r.Need(static_cast<size_t>(len))) {
        r.ok = false;
        break;
      }
      pr.value.bytes.assign(r.p + r.off, r.p + r.off + len);
      r.off += len;
      break;
    }
    default:
      r.ok = false;
      break;
  }
  return pr;
}

void PutNodeFull(std::vector<uint8_t>& b, const ViewNode& nd) {
  PutU32(b, nd.handle.object_id);
  PutU8(b, static_cast<uint8_t>(nd.handle.kind));
  PutU8(b, 0);
  PutU16(b, 0);
  PutU32(b, nd.parent_id);
  PutU32(b, nd.child_index);
  PutI32(b, nd.guest_view_id);
  PutStr16(b, Utf8ToUtf16(nd.class_name));
  PutU16(b, static_cast<uint16_t>(nd.props.size()));
  for (const Prop& pr : nd.props) PutProp(b, pr);
}

ViewNode GetNodeFull(Reader& r) {
  ViewNode nd;
  nd.handle.object_id = r.U32();
  nd.handle.kind = static_cast<HostObjectKind>(r.U8());
  r.U8();
  r.U16();
  nd.parent_id = r.U32();
  nd.child_index = r.U32();
  nd.guest_view_id = r.I32();
  nd.class_name = Str16ToUtf8(r);
  uint16_t props = r.U16();
  nd.props.reserve(props);
  for (uint16_t i = 0; i < props && r.ok; i++) nd.props.push_back(GetProp(r));
  return nd;
}

void PutDrawCmd(std::vector<uint8_t>& b, const DrawCommand& c) {
  PutU8(b, static_cast<uint8_t>(c.op));
  PutU8(b, 0);
  PutU16(b, 0);
  for (int i = 0; i < 4; i++) PutF32(b, c.f[i]);
  for (int i = 0; i < 8; i++) PutI32(b, c.i[i]);
  PutU32(b, c.color);
  if (c.op == DrawOp::kDrawText) {
    PutStr16(b, c.text);
  } else if (c.op == DrawOp::kDrawBitmap) {
    PutU32(b, static_cast<uint32_t>(c.bits.size()));
    for (uint32_t px : c.bits) PutU32(b, px);
  } else {
    PutU32(b, 0);
  }
}

DrawCommand GetDrawCmd(Reader& r) {
  DrawCommand c;
  c.op = static_cast<DrawOp>(r.U8());
  r.U8();
  r.U16();
  for (int i = 0; i < 4; i++) c.f[i] = r.F32();
  for (int i = 0; i < 8; i++) c.i[i] = r.I32();
  c.color = r.U32();
  if (c.op == DrawOp::kDrawText) {
    c.text = r.Str16();
  } else if (c.op == DrawOp::kDrawBitmap) {
    uint32_t bits_len = r.U32();
    if (!r.Need(static_cast<size_t>(bits_len) * 4)) {
      r.ok = false;
      return c;
    }
    c.bits.resize(bits_len);
    for (uint32_t i = 0; i < bits_len; i++) c.bits[i] = r.U32();
  } else {
    r.U32();
  }
  return c;
}

void RebuildChildren(ViewSnapshot& snap) {
  std::map<uint32_t, size_t> index;
  for (size_t i = 0; i < snap.nodes.size(); i++) {
    index[snap.nodes[i].handle.object_id] = i;
    snap.nodes[i].children.clear();
  }
  for (ViewNode& nd : snap.nodes) {
    auto it = index.find(nd.parent_id);
    if (it != index.end()) snap.nodes[it->second].children.push_back(nd.handle.object_id);
  }
}

constexpr uint32_t kMagic = 0x52424955u;  // "UIBR" 小端
constexpr uint16_t kVersion = 1;
constexpr uint8_t kKindFull = 1;
constexpr uint8_t kKindPatch = 2;

std::vector<uint8_t> MakeEnvelope(uint8_t kind, uint32_t revision,
                                  const std::vector<uint8_t>& snapshot_seg,
                                  const std::vector<uint8_t>& draw_seg) {
  std::vector<uint8_t> b;
  b.reserve(24 + snapshot_seg.size() + draw_seg.size());
  PutU32(b, kMagic);
  PutU16(b, kVersion);
  PutU8(b, kind);
  PutU8(b, 0);
  PutU32(b, revision);
  PutU32(b, static_cast<uint32_t>(snapshot_seg.size()));
  PutU32(b, static_cast<uint32_t>(draw_seg.size()));
  b.insert(b.end(), snapshot_seg.begin(), snapshot_seg.end());
  b.insert(b.end(), draw_seg.begin(), draw_seg.end());
  return b;
}

std::vector<uint8_t> SerializeDraw(const std::vector<DrawCommand>& draw) {
  std::vector<uint8_t> b;
  PutU32(b, static_cast<uint32_t>(draw.size()));
  for (const DrawCommand& c : draw) PutDrawCmd(b, c);
  return b;
}

// ---- 节点属性 diff ----

const Prop* FindProp(const ViewNode& nd, uint16_t key) {
  for (const Prop& pr : nd.props) {
    if (pr.key == key) return &pr;
  }
  return nullptr;
}

}  // namespace

// ---- PropValue ----

PropValue PropValue::Int(int32_t v) {
  PropValue p;
  p.type = PropType::kInt;
  p.i = v;
  return p;
}
PropValue PropValue::Float(float v) {
  PropValue p;
  p.type = PropType::kFloat;
  p.f = v;
  return p;
}
PropValue PropValue::Bool(bool v) {
  PropValue p;
  p.type = PropType::kBool;
  p.i = v ? 1 : 0;
  return p;
}
PropValue PropValue::String(std::string v) {
  PropValue p;
  p.type = PropType::kString;
  p.s = std::move(v);
  return p;
}
PropValue PropValue::Rect(int32_t l, int32_t t, int32_t r, int32_t b) {
  PropValue p;
  p.type = PropType::kRect;
  p.rect[0] = l;
  p.rect[1] = t;
  p.rect[2] = r;
  p.rect[3] = b;
  return p;
}
PropValue PropValue::Point(int32_t x, int32_t y) {
  PropValue p;
  p.type = PropType::kPoint;
  p.rect[0] = x;
  p.rect[1] = y;
  return p;
}
PropValue PropValue::Bytes(std::vector<uint8_t> v) {
  PropValue p;
  p.type = PropType::kBytes;
  p.bytes = std::move(v);
  return p;
}

bool PropValue::operator==(const PropValue& o) const {
  if (type != o.type) return false;
  switch (type) {
    case PropType::kInt:
    case PropType::kBool:
      return i == o.i;
    case PropType::kFloat:
      return f == o.f;
    case PropType::kString:
      return s == o.s;
    case PropType::kRect:
      return std::memcmp(rect, o.rect, sizeof(rect)) == 0;
    case PropType::kPoint:
      return rect[0] == o.rect[0] && rect[1] == o.rect[1];
    case PropType::kHandle:
      return handle.object_id == o.handle.object_id && handle.kind == o.handle.kind;
    case PropType::kBytes:
      return bytes == o.bytes;
  }
  return false;
}

// ---- 序列化 ----

std::vector<uint8_t> SerializeFull(const ViewSnapshot& snap,
                                   const std::vector<DrawCommand>& draw) {
  std::vector<uint8_t> seg;
  PutU32(seg, static_cast<uint32_t>(snap.nodes.size()));
  for (const ViewNode& nd : snap.nodes) PutNodeFull(seg, nd);
  return MakeEnvelope(kKindFull, snap.revision, seg, SerializeDraw(draw));
}

std::vector<uint8_t> SerializePatch(const ViewPatch& patch,
                                    const std::vector<DrawCommand>& draw) {
  std::vector<uint8_t> seg;
  PutU32(seg, patch.base_revision);
  PutU32(seg, static_cast<uint32_t>(patch.ops.size()));
  for (const NodePatch& op : patch.ops) {
    PutU8(seg, static_cast<uint8_t>(op.op));
    PutU8(seg, 0);
    PutU16(seg, 0);
    PutU32(seg, op.handle.object_id);
    PutU8(seg, static_cast<uint8_t>(op.handle.kind));
    PutU8(seg, 0);
    PutU16(seg, 0);
    PutU32(seg, op.parent_id);
    PutU32(seg, op.child_index);
    PutI32(seg, op.guest_view_id);
    PutStr16(seg, Utf8ToUtf16(op.class_name));
    PutU16(seg, static_cast<uint16_t>(op.props.size()));
    for (const Prop& pr : op.props) PutProp(seg, pr);
  }
  return MakeEnvelope(kKindPatch, patch.revision, seg, SerializeDraw(draw));
}

bool ParseEnvelope(const std::vector<uint8_t>& bytes, ViewSnapshot* full,
                   ViewPatch* patch, std::vector<DrawCommand>* draw,
                   uint32_t* revision, bool* is_patch) {
  Reader r{bytes.data(), bytes.size()};
  if (r.U32() != kMagic) return false;
  if (r.U16() != kVersion) return false;
  uint8_t kind = r.U8();
  r.U8();
  uint32_t rev = r.U32();
  uint32_t snap_bytes = r.U32();
  uint32_t draw_bytes = r.U32();
  if (!r.ok) return false;
  if (r.off + snap_bytes + draw_bytes != bytes.size()) return false;
  if (revision != nullptr) *revision = rev;
  bool is_p = kind == kKindPatch;
  if (is_patch != nullptr) *is_patch = is_p;

  Reader sr{r.p + r.off, snap_bytes};
  if (kind == kKindFull) {
    ViewSnapshot local;
    ViewSnapshot* target = full != nullptr ? full : &local;
    target->revision = rev;
    target->nodes.clear();
    uint32_t count = sr.U32();
    if (!sr.ok) return false;
    target->nodes.reserve(count);
    for (uint32_t i = 0; i < count && sr.ok; i++) target->nodes.push_back(GetNodeFull(sr));
    if (!sr.ok) return false;
    RebuildChildren(*target);
  } else if (kind == kKindPatch) {
    ViewPatch local;
    ViewPatch* target = patch != nullptr ? patch : &local;
    target->revision = rev;
    target->ops.clear();
    target->base_revision = sr.U32();
    uint32_t count = sr.U32();
    if (!sr.ok) return false;
    for (uint32_t i = 0; i < count && sr.ok; i++) {
      NodePatch op;
      op.op = static_cast<PatchOpKind>(sr.U8());
      sr.U8();
      sr.U16();
      op.handle.object_id = sr.U32();
      op.handle.kind = static_cast<HostObjectKind>(sr.U8());
      sr.U8();
      sr.U16();
      op.parent_id = sr.U32();
      op.child_index = sr.U32();
      op.guest_view_id = sr.I32();
      op.class_name = Str16ToUtf8(sr);
      uint16_t props = sr.U16();
      for (uint16_t k = 0; k < props && sr.ok; k++) op.props.push_back(GetProp(sr));
      if (!sr.ok) return false;
      target->ops.push_back(std::move(op));
    }
    if (!sr.ok) return false;
  } else {
    return false;
  }

  if (draw != nullptr) {
    *draw = ParseDrawSegment(r.p + r.off + snap_bytes, draw_bytes);
    // ParseDrawSegment 遇坏数据返回空——与"0 命令"无法区分，这里再校验一遍。
    Reader dr{r.p + r.off + snap_bytes, draw_bytes};
    uint32_t count = dr.U32();
    for (uint32_t i = 0; i < count && dr.ok; i++) GetDrawCmd(dr);
    if (!dr.ok) return false;
  }
  return true;
}

std::vector<DrawCommand> ParseDrawSegment(const uint8_t* data, uint32_t len) {
  std::vector<DrawCommand> out;
  Reader r{data, len};
  uint32_t count = r.U32();
  if (!r.ok) return out;
  out.reserve(count);
  for (uint32_t i = 0; i < count && r.ok; i++) out.push_back(GetDrawCmd(r));
  if (!r.ok) out.clear();
  return out;
}

bool ApplyPatch(const ViewSnapshot& base, const ViewPatch& patch, ViewSnapshot* out) {
  if (out == nullptr) return false;
  if (patch.base_revision != 0 && patch.base_revision != base.revision) return false;
  std::map<uint32_t, ViewNode> nodes;
  std::map<uint32_t, size_t> order;  // 保留基线先根序
  size_t seq = 0;
  for (const ViewNode& nd : base.nodes) {
    nodes[nd.handle.object_id] = nd;
    order[nd.handle.object_id] = seq++;
  }
  for (const NodePatch& op : patch.ops) {
    switch (op.op) {
      case PatchOpKind::kAdd: {
        ViewNode nd;
        nd.handle = op.handle;
        nd.guest_view_id = op.guest_view_id;
        nd.class_name = op.class_name;
        nd.parent_id = op.parent_id;
        nd.child_index = op.child_index;
        nd.props = op.props;
        nodes[nd.handle.object_id] = nd;
        order[nd.handle.object_id] = seq++;
        break;
      }
      case PatchOpKind::kRemove: {
        nodes.erase(op.handle.object_id);
        order.erase(op.handle.object_id);
        break;
      }
      case PatchOpKind::kUpdate: {
        auto it = nodes.find(op.handle.object_id);
        if (it == nodes.end()) return false;
        for (const Prop& pr : op.props) {
          bool replaced = false;
          for (Prop& have : it->second.props) {
            if (have.key == pr.key) {
              have = pr;
              replaced = true;
              break;
            }
          }
          if (!replaced) it->second.props.push_back(pr);
        }
        if (op.guest_view_id != -1) it->second.guest_view_id = op.guest_view_id;
        break;
      }
    }
  }
  // 重建先根展开序（父在子前；同父按 child_index）。
  out->revision = patch.revision;
  out->nodes.clear();
  std::map<uint32_t, std::vector<uint32_t>> kids;
  std::vector<uint32_t> roots;
  for (auto& [id, nd] : nodes) {
    if (nodes.count(nd.parent_id) != 0) {
      kids[nd.parent_id].push_back(id);
    } else {
      roots.push_back(id);
    }
    nd.children.clear();
  }
  for (auto& [pid, list] : kids) {
    std::sort(list.begin(), list.end(), [&](uint32_t a, uint32_t b) {
      if (nodes[a].child_index != nodes[b].child_index) {
        return nodes[a].child_index < nodes[b].child_index;
      }
      return order[a] < order[b];
    });
    nodes[pid].children = list;
  }
  std::function<void(uint32_t)> emit = [&](uint32_t id) {
    out->nodes.push_back(nodes[id]);
    for (uint32_t c : nodes[id].children) emit(c);
  };
  std::sort(roots.begin(), roots.end(), [&](uint32_t a, uint32_t b) {
    return order[a] < order[b];
  });
  for (uint32_t id : roots) emit(id);
  return true;
}

// ---- revision 管理 ----

ViewPatch DiffSnapshots(const ViewSnapshot& base, const ViewSnapshot& next) {
  ViewPatch patch;
  patch.base_revision = base.revision;
  patch.revision = next.revision;
  std::map<uint32_t, const ViewNode*> old_nodes;
  for (const ViewNode& nd : base.nodes) old_nodes[nd.handle.object_id] = &nd;
  std::map<uint32_t, bool> seen;
  for (const ViewNode& nd : next.nodes) {
    seen[nd.handle.object_id] = true;
    auto it = old_nodes.find(nd.handle.object_id);
    if (it == old_nodes.end()) {
      NodePatch op;
      op.op = PatchOpKind::kAdd;
      op.handle = nd.handle;
      op.guest_view_id = nd.guest_view_id;
      op.class_name = nd.class_name;
      op.parent_id = nd.parent_id;
      op.child_index = nd.child_index;
      op.props = nd.props;
      patch.ops.push_back(std::move(op));
      continue;
    }
    const ViewNode& old = *it->second;
    // 结构位移（换父）：退化为 Remove + Add（P1 口径）。
    if (old.parent_id != nd.parent_id) {
      NodePatch rm;
      rm.op = PatchOpKind::kRemove;
      rm.handle = nd.handle;
      patch.ops.push_back(rm);
      NodePatch op;
      op.op = PatchOpKind::kAdd;
      op.handle = nd.handle;
      op.guest_view_id = nd.guest_view_id;
      op.class_name = nd.class_name;
      op.parent_id = nd.parent_id;
      op.child_index = nd.child_index;
      op.props = nd.props;
      patch.ops.push_back(std::move(op));
      continue;
    }
    NodePatch up;
    up.op = PatchOpKind::kUpdate;
    up.handle = nd.handle;
    for (const Prop& pr : nd.props) {
      const Prop* old_pr = FindProp(old, pr.key);
      if (old_pr == nullptr || !(old_pr->value == pr.value)) up.props.push_back(pr);
    }
    // guest_view_id 变更随 Update 行（-1 = 未变更；ApplyPatch 同口径）。
    if (old.guest_view_id != nd.guest_view_id) {
      up.guest_view_id = nd.guest_view_id;
    }
    if (!up.props.empty() || up.guest_view_id != -1) patch.ops.push_back(std::move(up));
  }
  for (const ViewNode& nd : base.nodes) {
    if (seen.count(nd.handle.object_id) == 0) {
      NodePatch rm;
      rm.op = PatchOpKind::kRemove;
      rm.handle = nd.handle;
      patch.ops.push_back(rm);
    }
  }
  return patch;
}

std::vector<uint8_t> ViewTreeMirror::Submit(const ViewSnapshot& next,
                                            const std::vector<DrawCommand>& draw) {
  revision_++;
  ViewSnapshot snap = next;
  snap.revision = revision_;
  std::vector<uint8_t> env;
  if (!has_baseline_) {
    has_baseline_ = true;
    last_ = snap;
    return SerializeFull(snap, draw);
  }
  ViewPatch patch = DiffSnapshots(last_, snap);
  if (patch.ops.size() >= snap.nodes.size()) {
    last_ = snap;
    return SerializeFull(snap, draw);
  }
  last_ = snap;
  return SerializePatch(patch, draw);
}

void ViewTreeMirror::Reset() {
  last_ = ViewSnapshot{};
  has_baseline_ = false;
  revision_ = 0;
}

// ---- viewId 映射 ----

std::map<int32_t, uint32_t> BuildViewIdMap(const ViewSnapshot& snap) {
  std::map<int32_t, uint32_t> m;
  for (const ViewNode& nd : snap.nodes) {
    if (nd.guest_view_id != -1) m.emplace(nd.guest_view_id, nd.handle.object_id);
  }
  return m;
}

const ViewNode* FindByGuestId(const ViewSnapshot& snap, int32_t guest_view_id) {
  for (const ViewNode& nd : snap.nodes) {
    if (nd.guest_view_id == guest_view_id) return &nd;
  }
  return nullptr;
}

// guest ImageView 资源 id -> APK 内资源文件字节（PNG 等；宿主解码出画）。
// app APK 取 default_app loader 的 resource_zips（设备/宿主单 app 会话同源）。
bool LoadImageResourceBytes(VMState& state, uint32_t res_id, std::vector<uint8_t>* out) {
  static std::map<std::string, std::vector<uint8_t>> cache;
  if (state.linker == nullptr || out == nullptr || res_id == 0) return false;
  std::string apk;
  session::LoaderState* st = state.linker->loaders().default_app();
  for (auto it = st->resource_zips.rbegin(); it != st->resource_zips.rend(); ++it) {
    if (it->size() >= 4 && it->compare(it->size() - 4, 4, ".apk") == 0) {
      apk = *it;
      break;
    }
  }
  if (apk.empty()) return false;
  const std::string key = apk + "#" + std::to_string(res_id);
  auto hit = cache.find(key);
  if (hit != cache.end()) {
    *out = hit->second;
    return !out->empty();
  }
  const dexvm::runtime::ResourceSeam* seam = dexvm::runtime::GetResourceSeam();
  if (seam == nullptr || seam->open_resource_file == nullptr) return false;
  std::string entry_path;
  std::string err;
  std::vector<uint8_t> bytes;
  if (!seam->open_resource_file(apk, res_id, &entry_path, &bytes, &err)) return false;
  if (bytes.empty() || bytes.size() > (4u << 20)) return false;
  cache.emplace(key, bytes);
  *out = std::move(bytes);
  return true;
}

// ---- guest View 树 -> 快照 ----

namespace {
// object_id 必须随 guest View 身份稳定：位置序 id 在树结构变化（插入/删除
// 子树、RecyclerView 复用 item）时整体错位，DiffSnapshots 会把"新节点"
// 误判成"旧节点属性更新"，宿主镜像因此丢失新子树或保留错误类/父级
// （底部导航/空视图子项不物化、"布局有些乱"的根因）。指针哈希在同一
// 快照内用 map 兜底防碰撞。
uint32_t StableViewId(dexvm::runtime::VObject* view) {
  uint64_t h = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(view)) >> 4;
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53ULL;
  h ^= h >> 33;
  uint32_t id = static_cast<uint32_t>(h);
  if (id == 0u || id == 0xFFFFFFFFu) id ^= 0xa5a5a5a5u;
  return id;
}
}  // namespace

ViewSnapshot BuildSnapshotFromGuest(VMState& state, VObject* content_view) {
  (void)state;
  ViewSnapshot snap;
  if (content_view == nullptr) return snap;
  // 点击登记表代号推进（walk 到的视图标记为本代活视图；离树旧视图随后淘汰）。
  BeginGuestSnapshotWalk();
  std::map<uint32_t, VObject*> used_ids;
  std::function<void(VObject*, uint32_t, uint32_t)> walk =
      [&](VObject* view, uint32_t parent_id, uint32_t child_index) {
        if (view == nullptr) return;
        ViewNode nd;
        uint32_t object_id = StableViewId(view);
        auto clash = used_ids.find(object_id);
        while (clash != used_ids.end() && clash->second != view) {
          ++object_id;
          if (object_id == 0u || object_id == 0xFFFFFFFFu) object_id = 1;
          clash = used_ids.find(object_id);
        }
        used_ids.emplace(object_id, view);
        nd.handle.object_id = object_id;
        nd.parent_id = parent_id;
        nd.child_index = child_index;
        nd.guest_view_id = ViewI(view, "mId");
        // 类名（descriptor 末段）：Landroid/widget/TextView; -> "TextView"
        const std::string& desc = view->klass != nullptr ? view->klass->descriptor : "";
        size_t slash = desc.rfind('/');
        size_t semi = desc.rfind(';');
        if (semi == std::string::npos) semi = desc.size();
        nd.class_name = slash == std::string::npos
                            ? desc.substr(0, semi)
                            : desc.substr(slash + 1, semi - slash - 1);

        auto add = [&](uint16_t key, PropValue v) {
          Prop pr;
          pr.key = key;
          pr.value = std::move(v);
          nd.props.push_back(std::move(pr));
        };
        VString* text = reinterpret_cast<VString*>(ViewRef(view, "mText"));
        if (text != nullptr) {
          add(kPropText,
              PropValue::String(Utf16ToUtf8(text->chars(), text->length)));
        }
        NoteGuestView(view, object_id);  // object_id -> view 映射（点击回灌查路）
        add(kPropVisibility, PropValue::Int(ViewI(view, "mVisibility")));
        add(kPropBounds, PropValue::Rect(ViewI(view, "mLeft"), ViewI(view, "mTop"),
                                         ViewI(view, "mRight"), ViewI(view, "mBottom")));
        add(kPropPadding,
            PropValue::Rect(ViewI(view, "mPaddingLeft"), ViewI(view, "mPaddingTop"),
                            ViewI(view, "mPaddingRight"), ViewI(view, "mPaddingBottom")));
        add(kPropTextSize, PropValue::Float(ViewF(view, "mTextSize")));
        add(kPropTextColor, PropValue::Int(ViewI(view, "mTextColor")));
        add(kPropBackgroundColor, PropValue::Int(ViewI(view, "mBackgroundColor")));
        add(kPropAlpha, PropValue::Float(ViewF(view, "mAlpha")));
        add(kPropScroll, PropValue::Point(ViewI(view, "mScrollX"), ViewI(view, "mScrollY")));
        add(kPropMeasured,
            PropValue::Point(ViewI(view, "mMeasuredWidth"), ViewI(view, "mMeasuredHeight")));
        add(kPropOrientation, PropValue::Int(ViewI(view, "mOrientation")));
        add(kPropLayoutKind, PropValue::Int(LayoutKindOf(view)));
        VObject* lp = reinterpret_cast<VObject*>(ViewRef(view, "mLayoutParams"));
        if (lp != nullptr) {
          add(kPropLayoutParams, PropValue::Point(ViewI(lp, "width"), ViewI(lp, "height")));
          add(kPropLayoutGravity, PropValue::Int(ViewI(lp, "gravity")));
          add(kPropLayoutMargins,
              PropValue::Rect(ViewI(lp, "leftMargin"), ViewI(lp, "topMargin"),
                              ViewI(lp, "rightMargin"), ViewI(lp, "bottomMargin")));
          add(kPropLayoutWeight, PropValue::Float(ViewF(lp, "weight")));
        }
        add(kPropHasClickListener, PropValue::Bool(HasGuestClickListener(view)));
        add(kPropHasLongClickListener, PropValue::Bool(HasGuestLongClickListener(view)));
        add(kPropGravity, PropValue::Int(ViewI(view, "mGravity")));
        int32_t image_res = ViewI(view, "mImageResource");
        ObjHeader* drawable = ViewRef(view, "mDrawable");
        if (image_res == 0) {
          // setImageDrawable 路径（AppCompatImageView + Resources.getDrawable）：
          // drawable 侧表登记过 resId 时投影给宿主，由宿主持 guest APK 真实
          // 资源表装载（XML vector 图标/位图 drawable 的通用出画通路）。
          image_res = static_cast<int32_t>(dexvm::runtime::ResolveGuestDrawableResource(
              reinterpret_cast<VObject*>(drawable)));
        }
        add(kPropImageResource, PropValue::Int(image_res));
        if (image_res != 0) {
          std::vector<uint8_t> image_bytes;
          if (LoadImageResourceBytes(state, static_cast<uint32_t>(image_res), &image_bytes)) {
            add(kPropImageBytes, PropValue::Bytes(std::move(image_bytes)));
          }
        }
        add(kPropProgress, PropValue::Int(ViewI(view, "mProgress")));
        // RatingBar：rating/numStars（宿主原生星级拖拽回灌）。
        if (FindField(view->klass, "mRating") != nullptr) {
          add(kPropRating, PropValue::Float(ViewF(view, "mRating")));
        }
        if (FindField(view->klass, "mNumStars") != nullptr) {
          add(kPropNumStars, PropValue::Int(ViewI(view, "mNumStars")));
        }
        // CompoundButton/CheckedTextView 选中态（宿主可勾选控件状态同步）。
        if (FindField(view->klass, "mChecked") != nullptr) {
          add(kPropChecked, PropValue::Bool(ViewI(view, "mChecked") != 0));
        }
        // DrawerLayout 开合态（宿主真 androidx DrawerLayout 按此状态开合；
        // 宿主手势开合经 nativeDispatchViewDrawer 反向回灌 guest）。
        if (FindField(view->klass, "mDrawerOpen") != nullptr) {
          add(kPropDrawerOpen, PropValue::Bool(ViewI(view, "mDrawerOpen") != 0));
        }
        // Toolbar：标题/子标题/导航点击/菜单项标题（宿主真 Toolbar 重建）。
        if (VString* title = AsGuestString(ViewRef(view, "mTitle"))) {
          add(kPropText, PropValue::String(Utf16ToUtf8(title->chars(), title->length)));
        }
        if (VString* subtitle = AsGuestString(ViewRef(view, "mSubtitle"))) {
          add(kPropSubtitle,
              PropValue::String(Utf16ToUtf8(subtitle->chars(), subtitle->length)));
        }
        if (FindField(view->klass, "mNavigationClickListener") != nullptr) {
          add(kPropNavClick,
              PropValue::Bool(ViewRef(view, "mNavigationClickListener") != nullptr));
        }
        if (VObject* menu = reinterpret_cast<VObject*>(ViewRef(view, "mMenu"))) {
          VArray* titles = reinterpret_cast<VArray*>(ViewRef(menu, "mTitleArray"));
          if (titles != nullptr) {
            add(kPropMenuItems, PropValue::String(JoinStringArray(titles)));
          }
        }
        // AutoCompleteTextView：建议项镜像（宿主重建原生下拉项）。
        if (VArray* suggestions = reinterpret_cast<VArray*>(ViewRef(view, "mSuggestions"))) {
          add(kPropSuggestions, PropValue::String(JoinStringArray(suggestions)));
        }
        // DatePicker / TimePicker：状态字段（宿主原生选择器同步）。
        if (FindField(view->klass, "mYear") != nullptr &&
            FindField(view->klass, "mMonth") != nullptr &&
            FindField(view->klass, "mDay") != nullptr) {
          add(kPropDate, PropValue::Rect(ViewI(view, "mYear"), ViewI(view, "mMonth"),
                                         ViewI(view, "mDay"), 0));
        }
        if (FindField(view->klass, "mHour") != nullptr &&
            FindField(view->klass, "mMinute") != nullptr) {
          add(kPropTime, PropValue::Point(ViewI(view, "mHour"), ViewI(view, "mMinute")));
        }
        VString* video_path = reinterpret_cast<VString*>(ViewRef(view, "mVideoPath"));
        if (video_path != nullptr && video_path->length > 0) {
          add(kPropVideoPath,
              PropValue::String(Utf16ToUtf8(video_path->chars(), video_path->length)));
          add(kPropVideoSeek, PropValue::Int(ViewI(view, "mVideoSeek")));
          add(kPropVideoPlaying, PropValue::Bool(ViewI(view, "mVideoPlaying") != 0));
        }
        add(kPropTranslation,
            PropValue::Point(static_cast<int32_t>(ViewF(view, "mTranslationX")),
                             static_cast<int32_t>(ViewF(view, "mTranslationY"))));

        uint32_t self_id = nd.handle.object_id;
        snap.nodes.push_back(std::move(nd));

        VArray* children = reinterpret_cast<VArray*>(ViewRef(view, "mChildren"));
        if (children != nullptr) {
          for (int32_t i = 0; i < children->length; i++) {
            ObjHeader* ref = nullptr;
            std::memcpy(&ref, children->data() + static_cast<size_t>(i) * 8, sizeof(ref));
            if (ref == nullptr) continue;
            walk(reinterpret_cast<VObject*>(ref), self_id, static_cast<uint32_t>(i));
          }
        }
      };
  walk(content_view, kNoObject, 0);
  // Dialog 窗口根：guest 侧登记的叠加内容树（Dialog 对象不出 guest；宿主只
  // 看到视图根 + kPropWindowDim）。逻辑窗口布局在 walk 前由 VM 完成。
  dexvm::runtime::LayoutDialogWindows(state);
  dexvm::runtime::DialogWindowInfo dialogs[8] = {};
  const size_t dialog_count = dexvm::runtime::CollectDialogWindows(dialogs, 8);
  for (size_t i = 0; i < dialog_count; i++) {
    if (dialogs[i].content == nullptr) continue;
    const size_t before = snap.nodes.size();
    walk(dialogs[i].content, kNoObject, static_cast<uint32_t>(i + 1));
    if (snap.nodes.size() > before) {
      Prop dim;
      dim.key = kPropWindowDim;
      dim.value = PropValue::Float(dialogs[i].dim);
      snap.nodes[before].props.push_back(std::move(dim));
    }
  }
  RebuildChildren(snap);  // children 向量（由 parent_id 重建）
  EndGuestSnapshotWalk();
  return snap;
}

// ---- 缝A 记录 sink ----

void ProtocolDrawSink::OnFrameBegin() {
  stream_.clear();
  DrawCommand c;
  c.op = DrawOp::kFrameBegin;
  stream_.push_back(c);
}

void ProtocolDrawSink::OnSave() {
  DrawCommand c;
  c.op = DrawOp::kSave;
  stream_.push_back(c);
}

void ProtocolDrawSink::OnRestore() {
  DrawCommand c;
  c.op = DrawOp::kRestore;
  stream_.push_back(c);
}

void ProtocolDrawSink::OnRestoreToCount(int32_t count) {
  DrawCommand c;
  c.op = DrawOp::kRestoreToCount;
  c.i[0] = count;
  stream_.push_back(c);
}

void ProtocolDrawSink::OnTranslate(float dx, float dy) {
  DrawCommand c;
  c.op = DrawOp::kTranslate;
  c.f[0] = dx;
  c.f[1] = dy;
  stream_.push_back(c);
}

void ProtocolDrawSink::OnConcatMatrix(const float m[6]) {
  DrawCommand c;
  c.op = DrawOp::kConcatMatrix;
  for (int i = 0; i < 4; i++) c.f[i] = m[i];
  c.i[0] = static_cast<int32_t>(m[4] * 4096.0f);  // 平移量定点化（i 槽载）
  c.i[1] = static_cast<int32_t>(m[5] * 4096.0f);
  stream_.push_back(c);
}

void ProtocolDrawSink::OnSetMatrix(const float m[6]) {
  DrawCommand c;
  c.op = DrawOp::kSetMatrix;
  for (int i = 0; i < 4; i++) c.f[i] = m[i];
  c.i[0] = static_cast<int32_t>(m[4] * 4096.0f);
  c.i[1] = static_cast<int32_t>(m[5] * 4096.0f);
  stream_.push_back(c);
}

void ProtocolDrawSink::OnClipRect(float l, float t, float r, float b) {
  DrawCommand c;
  c.op = DrawOp::kClipRect;
  c.f[0] = l;
  c.f[1] = t;
  c.f[2] = r;
  c.f[3] = b;
  stream_.push_back(c);
}

void ProtocolDrawSink::OnFillRect(float l, float t, float r, float b, uint32_t argb) {
  DrawCommand c;
  c.op = DrawOp::kFillRect;
  c.f[0] = l;
  c.f[1] = t;
  c.f[2] = r;
  c.f[3] = b;
  c.color = argb;
  stream_.push_back(c);
}

void ProtocolDrawSink::OnDrawText(const uint16_t* chars, int32_t len, int32_t x, int32_t y,
                                  float text_size, uint32_t argb) {
  DrawCommand c;
  c.op = DrawOp::kDrawText;
  c.f[0] = text_size;
  c.i[0] = x;
  c.i[1] = y;
  c.color = argb;
  if (chars != nullptr && len > 0) c.text.assign(chars, chars + len);
  stream_.push_back(std::move(c));
}

void ProtocolDrawSink::OnDrawBitmap(int32_t sw, int32_t sh, int32_t sl, int32_t st, int32_t sr,
                                    int32_t sb, float dl, float dt, float dr, float db,
                                    const int32_t* pixels) {
  DrawCommand c;
  c.op = DrawOp::kDrawBitmap;
  c.i[0] = sw;
  c.i[1] = sh;
  c.i[2] = sl;
  c.i[3] = st;
  c.i[4] = sr;
  c.i[5] = sb;
  c.f[0] = dl;
  c.f[1] = dt;
  c.f[2] = dr;
  c.f[3] = db;
  // VM int[] 8 字节/元素 -> 紧密 u32 拷贝（源矩形之外也保全量（P1 简化））。
  if (pixels != nullptr && sw > 0 && sh > 0) {
    c.bits.resize(static_cast<size_t>(sw) * static_cast<size_t>(sh));
    for (int32_t y = 0; y < sh; y++) {
      for (int32_t x = 0; x < sw; x++) {
        uint32_t px = 0;
        std::memcpy(&px, reinterpret_cast<const uint8_t*>(pixels) +
                             (static_cast<size_t>(y) * sw + x) * 8,
                    sizeof(px));
        c.bits[static_cast<size_t>(y) * sw + x] = px;
      }
    }
  }
  stream_.push_back(std::move(c));
}

void ProtocolDrawSink::OnDrawLine(float x0, float y0, float x1, float y1, float stroke,
                                  uint32_t argb) {
  DrawCommand c;
  c.op = DrawOp::kDrawLine;
  c.f[0] = x0;
  c.f[1] = y0;
  c.f[2] = x1;
  c.f[3] = y1;
  c.i[0] = static_cast<int32_t>(stroke * 4096.0f);
  c.color = argb;
  stream_.push_back(c);
}

void ProtocolDrawSink::OnDrawCircle(float cx, float cy, float r, bool fill, float stroke,
                                    uint32_t argb) {
  DrawCommand c;
  c.op = DrawOp::kDrawCircle;
  c.f[0] = cx;
  c.f[1] = cy;
  c.f[2] = r;
  c.i[0] = fill ? 1 : 0;
  c.i[1] = static_cast<int32_t>(stroke * 4096.0f);
  c.color = argb;
  stream_.push_back(c);
}

// ---- 全局面 ----

ProtocolDrawSink& GlobalProtocolSink() {
  static ProtocolDrawSink sink;
  return sink;
}

ViewTreeMirror& GlobalViewTreeMirror() {
  static ViewTreeMirror mirror;
  return mirror;
}

void ResetUiBridgeProtocol() {
  GlobalProtocolSink().Clear();
  GlobalViewTreeMirror().Reset();
}

bool ExportViewTree(VMState* state, VObject* content_view, int32_t width, int32_t height,
                    UIBridge* ui) {
  (void)width;
  (void)height;
  if (state == nullptr || content_view == nullptr || ui == nullptr) return false;
  ViewSnapshot snap = BuildSnapshotFromGuest(*state, content_view);
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DexVmViewTree", "present root=%s nodes=%zu",
                      content_view->klass != nullptr ? content_view->klass->descriptor.c_str()
                                                     : "?",
                      snap.nodes.size());
  // TEMP DIAG: 节点树明细（首帧/快照结构分析；事后移除）。
  for (size_t i = 0; i < snap.nodes.size(); i++) {
    const ViewNode& node = snap.nodes[i];
    __android_log_print(ANDROID_LOG_INFO, "DexVmViewTree", "  [%zu] %s id=0x%x parent=%u",
                        i, node.class_name.c_str(), node.guest_view_id, node.parent_id);
  }
#endif
  if (snap.nodes.empty()) return false;
  std::vector<uint8_t> env =
      GlobalViewTreeMirror().Submit(snap, GlobalProtocolSink().stream());
  return ui->PresentViewTree(std::move(env));
}

void RegisterUiBridgeProtocol() {
  SetViewTreeExporter(&ExportViewTree);
  SetGlobalDrawSink(&GlobalProtocolSink());
}

namespace {
// 加载自注册（app：ui_bridge_jni.cc 锚定本 TU；测试：显式 Register 亦可）。
struct ProtocolAutoRegister {
  ProtocolAutoRegister() { RegisterUiBridgeProtocol(); }
} g_protocol_auto_register;
}  // namespace

}  // namespace dexvm::bridge
