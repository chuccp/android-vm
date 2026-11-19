// android.view.LayoutInflater 面（资源布局膨胀）——VM native 缝档。
// Activity.setContentView(int) / Window.setContentView(int) 的落点：
//   resources.arsc 查 layout 文件（session::ReadResourceFile）
//   -> 编译态 binary XML 事件流（session::ParseResourceXml）
//   -> guest View 树（内建 widget 类；未知容器/ConstraintLayout 以
//      LinearLayout(vertical) 近似承载，约束属性不解释——桥接出画近似面）
//   -> Activity.mContentView（首帧管线 traversal/present 同径）。
//
// 语义口径（docs 20 章 / §31.7）：字节解析归 session 缝；本 TU 只做
// AXML 事件 -> guest View 属性映射（android:* 常用面）。@string/@color/
// @dimen 经 ResourceSeam.lookup_value 解析（reference 链已由 session 收口）。
// 不解释：style/theme（?attr/...）、drawable 资源、约束/相对布局规则。
#include <cmath>
#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#endif
// 每次 inflate 的整树 dump（逐节点 logcat）默认关闭：RecyclerView item 反复
// inflate 时是秒级纯开销；需要复现布局树时以 -DDEXVM_INFLATE_DUMP=1 构建。
#ifndef DEXVM_INFLATE_DUMP
#define DEXVM_INFLATE_DUMP 0
#endif
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/builtins_internal.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/framework_builtins_internal.h"
#include "core/interpreter.h"
#include "core/intrinsic.h"
#include "core/vm_types.h"

namespace dexvm::runtime {
namespace {

constexpr uint8_t kValueReference = 0x01;
constexpr uint8_t kValueAttribute = 0x02;
constexpr uint8_t kValueString = 0x03;
constexpr uint8_t kValueFloat = 0x04;
constexpr uint8_t kValueDimension = 0x05;
constexpr uint8_t kValueFraction = 0x06;
constexpr uint8_t kValueDynamicReference = 0x07;

constexpr int32_t kMatchParent = -1;
constexpr int32_t kWrapContent = -2;

// 常用 framework 属性 id -> 名（编译态 string pool 未留名时的归名面）。
const char* AttrNameById(uint32_t id) {
  switch (id) {
    case 0x010100d0: return "id";
    case 0x010100d4: return "background";
    case 0x010100d5: return "padding";
    case 0x010100d6: return "paddingLeft";
    case 0x010100d7: return "paddingTop";
    case 0x010100d8: return "paddingRight";
    case 0x010100d9: return "paddingBottom";
    case 0x010100dc: return "visibility";
    case 0x010100af: return "gravity";
    case 0x010100c4: return "orientation";
    case 0x010100f4: return "layout_width";
    case 0x010100f5: return "layout_height";
    case 0x010100f7: return "layout_margin";
    case 0x010100f8: return "layout_marginTop";
    case 0x010100f9: return "layout_marginBottom";
    case 0x010100fa: return "layout_marginLeft";
    case 0x010100fb: return "layout_marginRight";
    case 0x0101014f: return "text";
    case 0x01010095: return "textSize";
    case 0x01010098: return "textColor";
    case 0x01010119: return "src";
    case 0x0101013f: return "textStyle";
    case 0x01010106: return "checked";
    case 0x01010137: return "progress";
    default: return nullptr;
  }
}

std::string_view AttrName(const session::XmlResAttr& a) {
  if (!a.name.empty()) return a.name;
  const char* n = AttrNameById(a.res_id);
  return n != nullptr ? std::string_view(n) : std::string_view();
}

std::u16string Utf8ToUtf16(const std::string& s) {
  std::u16string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    uint32_t cp = static_cast<unsigned char>(s[i]);
    if (cp < 0x80) {
      out.push_back(static_cast<char16_t>(cp));
      i += 1;
    } else if ((cp & 0xE0) == 0xC0 && i + 1 < s.size()) {
      cp = ((cp & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
      out.push_back(static_cast<char16_t>(cp));
      i += 2;
    } else if ((cp & 0xF0) == 0xE0 && i + 2 < s.size()) {
      cp = ((cp & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
           (static_cast<unsigned char>(s[i + 2]) & 0x3F);
      out.push_back(static_cast<char16_t>(cp));
      i += 3;
    } else if ((cp & 0xF8) == 0xF0 && i + 3 < s.size()) {
      cp = ((cp & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
           ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
           (static_cast<unsigned char>(s[i + 3]) & 0x3F);
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
      i += 4;
    } else {
      i += 1;  // 非法字节跳过（资源串实测无此面）
    }
  }
  return out;
}

// Res_value dimension complex -> px（guest 像素面 1:1；dp/sp 密度近似 1.0，
// 与桥接 540x960 guest 像素窗口径一致）。
float ComplexToFloat(uint32_t data) {
  return static_cast<float>(static_cast<int32_t>(data & 0xFFFFFF00u)) / 256.0f;
}

float DimensionToPx(uint32_t data) {
  const int unit = static_cast<int>(data & 0xF);
  const float v = ComplexToFloat(data);
  switch (unit) {
    case 1:  // dp
    case 2:  // sp
      return v;
    case 3:  // pt
      return v * 160.0f / 72.0f;
    case 4:  // in
      return v * 160.0f;
    case 5:  // mm
      return v * 160.0f / 25.4f;
    default:  // px
      return v;
  }
}

bool ParseColorString(const std::string& s, uint32_t* out) {
  if (s.size() < 2 || s[0] != '#') return false;
  uint32_t v = 0;
  for (size_t i = 1; i < s.size(); i++) {
    char c = s[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = (v << 4) | static_cast<uint32_t>(d);
  }
  switch (s.size() - 1) {
    case 3: {  // #RGB
      uint32_t r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
      *out = 0xFF000000u | (r * 0x11 << 16) | (g * 0x11 << 8) | (b * 0x11);
      return true;
    }
    case 4: {  // #ARGB
      uint32_t a = (v >> 12) & 0xF, r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
      *out = (a * 0x11 << 24) | (r * 0x11 << 16) | (g * 0x11 << 8) | (b * 0x11);
      return true;
    }
    case 6: *out = 0xFF000000u | v; return true;  // #RRGGBB
    case 8: *out = v; return true;                // #AARRGGBB
    default: return false;
  }
}

struct InflateContext {
  VMState& state;
  VObject* activity = nullptr;
  std::string apk_path;
  std::string error;
  // 发起 inflate 的 LayoutInflater（AOSP createView 的 Factory2/Factory 咨询面；
  // app 自定义 Factory 会把 TextView 等标签替换为自家子类（ZHTextView）——
  // 不咨询 = 后面 findViewById 强转 CCE）。
  VObject* inflater = nullptr;
};

const ResourceSeam* Seam() {
  static const ResourceSeam kEmpty{};
  const ResourceSeam* seam = GetResourceSeam();
  return seam != nullptr ? seam : &kEmpty;
}

bool EndsWith(const std::string& s, const char* suffix) {
  size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// app APK 路径：Activity 类 loader 的 resource_zips 优先，其次 default_app。
std::string ResolveApkPath(ClassLinker* linker, VObject* activity) {
  auto scan = [](session::LoaderState* st) -> std::string {
    for (auto it = st->resource_zips.rbegin(); it != st->resource_zips.rend(); ++it) {
      if (EndsWith(*it, ".apk")) return *it;
    }
    return {};
  };
  if (activity != nullptr && activity->klass != nullptr && activity->klass->class_loader != nullptr) {
    if (session::LoaderState* st = linker->loaders().Lookup(activity->klass->class_loader)) {
      if (std::string p = scan(st); !p.empty()) return p;
    }
  }
  return scan(linker->loaders().default_app());
}

// 类型化值解析：reference/dynamic 经资源表链解析；返回终值 (type, data, string)。
struct ResolvedValue {
  uint8_t type = 0;
  uint32_t data = 0;
  std::string string;
};

bool ResolveAttrValue(InflateContext& ctx, const session::XmlResAttr& attr, ResolvedValue* out) {
  out->type = attr.value_type;
  out->data = attr.value_data;
  out->string = attr.value;
  if (attr.value_type == kValueReference || attr.value_type == kValueDynamicReference) {
    const ResourceSeam* seam = Seam();
    if (seam->lookup_value == nullptr) return false;
    uint8_t t = 0;
    uint32_t d = 0;
    std::string s;
    std::string err;
    if (!seam->lookup_value(ctx.apk_path, attr.value_data, &t, &d, &s, &err)) return false;
    out->type = t;
    out->data = d;
    out->string = s;
    return true;
  }
  if (attr.value_type == kValueAttribute) return false;  // ?attr：无主题面
  return true;
}

bool ResolveStringValue(InflateContext& ctx, const session::XmlResAttr& attr, std::string* out) {
  ResolvedValue v;
  if (!ResolveAttrValue(ctx, attr, &v)) return false;
  if (!v.string.empty()) {
    *out = v.string;
    return true;
  }
  if (v.type == kValueString) {
    *out = attr.value;
    return !out->empty();
  }
  if (!attr.value.empty()) {
    *out = attr.value;  // rawValue 原文
    return true;
  }
  return false;
}

int32_t ResolveIntValue(InflateContext& ctx, const session::XmlResAttr& attr, int32_t fallback) {
  ResolvedValue v;
  if (!ResolveAttrValue(ctx, attr, &v)) return fallback;
  switch (v.type) {
    case 0x10:  // TYPE_INT_DEC
    case 0x11:  // TYPE_INT_HEX
    case 0x12:  // TYPE_INT_BOOLEAN
    case 0x1c:  // TYPE_INT_COLOR_ARGB8
    case 0x1d:  // TYPE_INT_COLOR_RGB8
    case 0x1e:  // TYPE_INT_COLOR_ARGB4
    case 0x1f:  // TYPE_INT_COLOR_RGB4
      return static_cast<int32_t>(v.data);
    default:
      return fallback;
  }
}

bool ResolveColorValue(InflateContext& ctx, const session::XmlResAttr& attr, int32_t* out) {
  ResolvedValue v;
  if (!ResolveAttrValue(ctx, attr, &v)) return false;
  switch (v.type) {
    case 0x1c:
    case 0x1d:
    case 0x1e:
    case 0x1f:
    case 0x10:
    case 0x11:
      *out = static_cast<int32_t>(v.data);
      return true;
    case kValueString: {
      uint32_t color = 0;
      if (ParseColorString(v.string, &color)) {
        *out = static_cast<int32_t>(color);
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

bool ResolveDimensionPx(InflateContext& ctx, const session::XmlResAttr& attr, float* out) {
  ResolvedValue v;
  if (!ResolveAttrValue(ctx, attr, &v)) return false;
  if (v.type == kValueDimension || v.type == kValueFraction) {
    *out = DimensionToPx(v.data);
    return true;
  }
  if (v.type == kValueFloat) {
    float f = 0;
    std::memcpy(&f, &v.data, sizeof(f));
    *out = f;
    return true;
  }
  if (v.type == 0x10 || v.type == 0x11) {
    *out = static_cast<float>(static_cast<int32_t>(v.data));
    return true;
  }
  return false;
}

// 布局参数尺寸语义：-1 match_parent / -2 wrap_content / 数值 px。
// ConstraintLayout 近似面：0dp = 约束匹配 -> 视作 match_parent。
bool ResolveSizeValue(InflateContext& ctx, const session::XmlResAttr& attr, int32_t* out) {
  ResolvedValue v;
  if (!ResolveAttrValue(ctx, attr, &v)) return false;
  if (v.type == 0x10 || v.type == 0x11) {
    *out = static_cast<int32_t>(v.data);
    return true;
  }
  if (v.type == kValueDimension || v.type == kValueFraction) {
    float px = DimensionToPx(v.data);
    *out = px == 0.0f ? kMatchParent : static_cast<int32_t>(std::lround(px));
    return true;
  }
  return false;
}

VField* FindFieldInChain(VClass* klass, const char* name) {
  return klass != nullptr ? FindInstanceField(klass, name) : nullptr;
}

void SetIntFieldByName(VObject* view, const char* name, int32_t value) {
  if (VField* f = FindFieldInChain(view->klass, name)) SetIField(view, f->offset, value);
}

void SetFloatFieldByName(VObject* view, const char* name, float value) {
  if (VField* f = FindFieldInChain(view->klass, name)) SetFField(view, f->offset, value);
}

bool IsViewGroupClass(VClass* klass) {
  VClass* vg = klass;  // descriptor chain 判断不做字符串比较，走字段存在性即可
  (void)vg;
  return FindFieldInChain(klass, "mChildren") != nullptr;
}

// tag -> 内建类 descriptor；未知类 → 空（走 guest 类/通用容器）。
std::string WidgetDescriptorForTag(std::string_view tag, bool* constraint_approx) {
  *constraint_approx = false;
  if (tag == "TextView") return "Landroid/widget/TextView;";
  if (tag == "Button") return "Landroid/widget/Button;";
  if (tag == "EditText") return "Landroid/widget/EditText;";
  if (tag == "Switch") return "Landroid/widget/Switch;";
  if (tag == "ImageView" || tag == "ImageButton") return "Landroid/widget/ImageView;";
  if (tag == "ProgressBar") return "Landroid/widget/ProgressBar;";
  if (tag == "VideoView") return "Landroid/widget/VideoView;";
  if (tag == "LinearLayout") return "Landroid/widget/LinearLayout;";
  if (tag == "FrameLayout") return "Landroid/widget/FrameLayout;";
  if (tag == "RelativeLayout") return "Landroid/widget/RelativeLayout;";
  if (tag == "ScrollView" || tag == "HorizontalScrollView") return "Landroid/widget/ScrollView;";
  if (tag == "View" || tag == "Space") return "Landroid/view/View;";
  if (tag == "ViewGroup") return "Landroid/view/ViewGroup;";
  return {};
}

VClass* FindGuestClass(VMState& state, VObject* activity, const std::string& tag) {
  if (tag.empty()) return nullptr;
  std::string desc;
  if (tag.front() == 'L' && tag.back() == ';') {
    desc = tag;
  } else {
    desc = "L";
    for (char c : tag) desc.push_back(c == '.' ? '/' : c);
    desc.push_back(';');
  }
  VObject* loader =
      activity != nullptr && activity->klass != nullptr ? activity->klass->class_loader : nullptr;
  return state.linker->FindClass(&state, loader, desc);
}

VObject* NewView(InflateContext& ctx, VClass* klass) {
  if (klass == nullptr) return nullptr;
  VObject* view = ctx.state.linker->AllocObject(klass);
  if (view == nullptr) return nullptr;
  // 构造器选取按 AOSP LayoutInflater 顺序（Context 恒为 activity；AttributeSet
  // 面以 null 传——属性值已在 inflate 属性映射里落字段）：
  //   (Context, AttributeSet) -> (Context, AttributeSet, int) -> (Context) ->
  //   (Context, AttributeSet, int, int)
  auto try_ctor = [&](const std::vector<std::string>& params) -> bool {
    for (auto& m : klass->direct_methods) {
      if (m.name != "<init>" || m.param_types != params) continue;
      std::vector<Value> full(params.size() + 1);
      full[0].ref = view;
      for (size_t i = 0; i < params.size(); i++) {
        if (params[i] == "Landroid/content/Context;") {
          full[i + 1].ref = ctx.activity;
        } else if (params[i] == "I") {
          full[i + 1].i = 0;
        } else {
          full[i + 1].ref = nullptr;  // AttributeSet / 其它引用面
        }
      }
      Value ret{};
      if (!InvokeMethod(ctx.state, &m, full.data(), full.size(), &ret)) {
#if defined(__ANDROID__)
        std::string ex_desc = "?";
        std::string ex_msg;
        if (ctx.state.pending_exception != nullptr &&
            ctx.state.pending_exception->klass != nullptr) {
          ex_desc = ctx.state.pending_exception->klass->descriptor;
          for (VClass* k = ctx.state.pending_exception->klass; k != nullptr;
               k = k->super_class) {
            bool done = false;
            for (auto& f : k->instance_fields) {
              if (f.name != "detailMessage") continue;
              ObjHeader* msg = nullptr;
              std::memcpy(&msg,
                          reinterpret_cast<VObject*>(ctx.state.pending_exception)->field_data() +
                              f.offset,
                          sizeof(msg));
              if (msg != nullptr) ex_msg = static_cast<VString*>(msg)->ToModifiedUtf8();
              done = true;
              break;
            }
            if (done) break;
          }
        }
        __android_log_print(ANDROID_LOG_INFO, "DexVmInflate", "ctor %s failed: %s: %s",
                            klass->descriptor.c_str(), ex_desc.c_str(), ex_msg.c_str());
        // 抛点 guest 栈（innermost 先）：定位构造器内部失败指令所在方法。
        const auto& bt = ctx.state.last_throw_backtrace;
        for (size_t i = 0; i < bt.size() && i < 16; i++) {
          __android_log_print(ANDROID_LOG_INFO, "DexVmInflate", "  at %s", bt[i].c_str());
        }
#endif
        ctx.state.ClearException();
        return false;
      }
      return true;
    }
    return false;
  };
  bool ctor_ok_now = false;
  if (try_ctor({"Landroid/content/Context;", "Landroid/util/AttributeSet;"}) ||
      try_ctor({"Landroid/content/Context;", "Landroid/util/AttributeSet;", "I"}) ||
      try_ctor({"Landroid/content/Context;"}) ||
      try_ctor({"Landroid/content/Context;", "Landroid/util/AttributeSet;", "I", "I"})) {
    ctor_ok_now = true;
  }
#if defined(__ANDROID__)
  if (klass->descriptor.find("DrawerLayout") != std::string::npos) {
    ObjHeader* ld = nullptr;
    if (VField* f = FindFieldInChain(klass, "mLeftDragger")) {
      ld = GetRefField(view, f->offset);
    }
    __android_log_print(ANDROID_LOG_INFO, "DexVmInflate", "DrawerLayout ctor_ok=%d dragger=%p",
                        ctor_ok_now ? 1 : 0, (void*)ld);
  }
#endif
  if (ctor_ok_now) return view;
  // 无 Context 构造（ViewGroup 骨架）：View 基类 init 语义补齐。
  if (VClass* view_cls = ctx.state.linker->FindClass("Landroid/view/View;")) {
    for (auto& m : view_cls->direct_methods) {
      if (m.name != "<init>") continue;
      if (m.param_types.size() != 1 || m.param_types[0] != "Landroid/content/Context;") continue;
      Value args[2] = {};
      args[0].ref = view;
      args[1].ref = ctx.activity;
      Value ret{};
      if (!InvokeMethod(ctx.state, &m, args, 2, &ret)) {
        ctx.state.ClearException();
        return nullptr;
      }
      return view;
    }
  }
  return view;
}

struct LayoutOverrides {
  int32_t width = kWrapContent;
  int32_t height = kWrapContent;
  int32_t margin_left = 0;
  int32_t margin_top = 0;
  int32_t margin_right = 0;
  int32_t margin_bottom = 0;
  bool any = false;
};

// 协变返回覆写查找：generateLayoutParams 家族子类覆写返回更具体的 LP 类型
// （AppBarLayout.LayoutParams 等），与探针的 ViewGroup$LayoutParams 精确签名
// 不等——FindInterfaceImpl 的 SameSignature 要求返回类型全等，故按 name+params
// 沿类链找最具体实现。
VMethod* FindCovariantMethod(VClass* klass, const char* name,
                             const std::vector<std::string>& params) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& m : k->virtual_methods)
      if (m.name == name && m.param_types == params) return &m;
    for (auto& m : k->direct_methods)
      if (!m.IsStatic() && m.name == name && m.param_types == params) return &m;
  }
  return nullptr;
}

void SetChildLayoutParams(InflateContext& ctx, VObject* parent, VObject* view,
                          const LayoutOverrides& ov) {
  // AOSP：子 View 的 LayoutParams 由父容器 generateLayoutParams(AttributeSet) 产
  // 生（各 ViewGroup 自有 LP 类型——DrawerLayout/RelativeLayout 等强转依赖）。
  VObject* lp = nullptr;
  if (parent != nullptr) {
    VMethod* impl = FindCovariantMethod(
        parent->klass, "generateLayoutParams", {"Landroid/util/AttributeSet;"});
    if (impl != nullptr) {
      Value av[2] = {};
      av[0].ref = parent;
      av[1].ref = nullptr;  // AttributeSet 面 null（属性值由本引擎字段映射落位）
      Value rv{};
      std::string ex_desc;
      if (InvokeMethod(ctx.state, impl, av, 2, &rv)) {
        lp = reinterpret_cast<VObject*>(rv.ref);
      } else {
#if defined(__ANDROID__)
        if (ctx.state.pending_exception != nullptr &&
            ctx.state.pending_exception->klass != nullptr) {
          ex_desc = ctx.state.pending_exception->klass->descriptor;
          for (VClass* c = ctx.state.pending_exception->klass; c != nullptr && ex_desc.find(": ") == std::string::npos;
               c = c->super_class) {
            for (auto& f : c->instance_fields) {
              if (f.name != "detailMessage") continue;
              ObjHeader* msg = nullptr;
              std::memcpy(&msg,
                          reinterpret_cast<VObject*>(ctx.state.pending_exception)->field_data() +
                              f.offset,
                          sizeof(msg));
              if (msg != nullptr) ex_desc += ": " + static_cast<VString*>(msg)->ToModifiedUtf8();
              break;
            }
          }
        }
#endif
        ctx.state.ClearException();
      }
#if defined(__ANDROID__)
      // 失败打点（成功静默——每条 inflate 全量打印曾拖慢启动）。
      if (lp == nullptr) {
        __android_log_print(
            ANDROID_LOG_INFO, "DexVmInflate", "LP parent=%s impl=%s -> null ok=0%s%s",
            parent->klass->descriptor.c_str(),
            impl->declaring != nullptr ? impl->declaring->descriptor.c_str() : "?",
            ex_desc.empty() ? "" : " ex=", ex_desc.c_str());
        if (!ex_desc.empty()) {
          const auto& bt = ctx.state.last_throw_backtrace;
          for (size_t i = 0; i < bt.size() && i < 6; i++) {
            __android_log_print(ANDROID_LOG_INFO, "DexVmInflate", "  LPex at %s", bt[i].c_str());
          }
        }
      }
#endif
    }
  }
  if (lp == nullptr) {
    VClass* lp_cls = ctx.state.linker->FindClass("Landroid/widget/LinearLayout$LayoutParams;");
    if (lp_cls == nullptr) return;
    lp = ctx.state.linker->AllocObject(lp_cls);
  }
  if (lp == nullptr) return;
  SetIntFieldByName(lp, "width", ov.width);
  SetIntFieldByName(lp, "height", ov.height);
  SetIntFieldByName(lp, "leftMargin", ov.margin_left);
  SetIntFieldByName(lp, "topMargin", ov.margin_top);
  SetIntFieldByName(lp, "rightMargin", ov.margin_right);
  SetIntFieldByName(lp, "bottomMargin", ov.margin_bottom);
  if (VField* f = FindFieldInChain(view->klass, "mLayoutParams")) {
    SetRefField(view, f->offset, reinterpret_cast<ObjHeader*>(lp));
  }
}

// AOSP LayoutInflater.createView 第一步：Factory2/Factory 咨询。
// 返回非空 View 表示工厂已创建（跳过类解析/构造）；nullptr = 未处理，走默认。
VObject* TryCreateViaFactory(InflateContext& ctx, VObject* parent_view, const std::string& tag) {
  VObject* inflater = ctx.inflater;
  if (inflater == nullptr || inflater->klass == nullptr) return nullptr;
  auto field_ref = [&](const char* name) -> VObject* {
    VField* f = FindFieldInChain(inflater->klass, name);
    if (f == nullptr) return nullptr;
    return reinterpret_cast<VObject*>(GetRefField(inflater, f->offset));
  };
  VObject* factory2 = field_ref("mFactory2");
  VObject* factory = factory2 != nullptr ? factory2 : field_ref("mFactory");
  if (factory == nullptr || factory->klass == nullptr) {
#if defined(__ANDROID__)
    // 一次性归因：inflater 上无 Factory（注册没落点）。
    static bool once = false;
    if (!once) {
      once = true;
      __android_log_print(ANDROID_LOG_INFO, "DexVmInflate", "F2 no factory inflater=%s tag=%s",
                          inflater->klass != nullptr ? inflater->klass->descriptor.c_str() : "?",
                          tag.c_str());
    }
#endif
    return nullptr;
  }
  VString* tag_str = ctx.state.linker->AllocString(std::u16string(tag.begin(), tag.end()));
  if (tag_str == nullptr) return nullptr;
  Value av[5] = {};
  size_t argc = 0;
  VMethod* m = nullptr;
  if (factory2 != nullptr) {
    VMethod probe;
    probe.name = "onCreateView";
    probe.param_types = {"Landroid/view/View;", "Ljava/lang/String;",
                         "Landroid/content/Context;", "Landroid/util/AttributeSet;"};
    probe.return_type = "Landroid/view/View;";
    m = factory2->klass->FindInterfaceImpl(&probe);
    if (m != nullptr) {
      av[0].ref = reinterpret_cast<ObjHeader*>(factory2);
      av[1].ref = reinterpret_cast<ObjHeader*>(parent_view);
      av[2].ref = reinterpret_cast<ObjHeader*>(tag_str);
      av[3].ref = reinterpret_cast<ObjHeader*>(ctx.activity);
      av[4].ref = nullptr;
      argc = 5;
    }
  }
  if (m == nullptr) {
    VMethod probe;
    probe.name = "onCreateView";
    probe.param_types = {"Ljava/lang/String;", "Landroid/content/Context;",
                         "Landroid/util/AttributeSet;"};
    probe.return_type = "Landroid/view/View;";
    m = factory->klass->FindInterfaceImpl(&probe);
    if (m != nullptr) {
      av[0].ref = reinterpret_cast<ObjHeader*>(factory);
      av[1].ref = reinterpret_cast<ObjHeader*>(tag_str);
      av[2].ref = reinterpret_cast<ObjHeader*>(ctx.activity);
      av[3].ref = nullptr;
      argc = 4;
    }
  }
  if (m == nullptr) return nullptr;
  Value rv{};
  bool ok = InvokeMethod(ctx.state, m, av, argc, &rv);
  std::string ex;
  if (!ok) {
    if (ctx.state.pending_exception != nullptr &&
        ctx.state.pending_exception->klass != nullptr) {
      ex = ctx.state.pending_exception->klass->descriptor;
    }
    ctx.state.ClearException();
  }
  VObject* out = ok ? reinterpret_cast<VObject*>(rv.ref) : nullptr;
#if defined(__ANDROID__)
  // 失败打点（成功静默）：工厂抛异常或返回 null 才记录。
  if (!ex.empty()) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmInflate",
                        "F2 tag=%s inflater=%s factory2=%s factory=%s -> null ok=0 ex=%s",
                        tag.c_str(),
                        inflater->klass != nullptr ? inflater->klass->descriptor.c_str() : "?",
                        factory2 != nullptr && factory2->klass != nullptr
                            ? factory2->klass->descriptor.c_str()
                            : "-",
                        factory != nullptr && factory->klass != nullptr
                            ? factory->klass->descriptor.c_str()
                            : "-",
                        ex.c_str());
    const auto& bt = ctx.state.last_throw_backtrace;
    for (size_t i = 0; i < bt.size() && i < 6; i++) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmInflate", "  F2ex at %s", bt[i].c_str());
    }
  }
#endif
  return out;
}

bool AddChild(InflateContext& ctx, VObject* parent, VObject* child) {
  if (parent == nullptr || child == nullptr) return false;
  if (!IsViewGroupClass(parent->klass)) return false;
  Value args[1] = {};
  args[0].ref = child;
  bool ok = true;
  VGAddView(ctx.state, parent, args, 1, &ok);
  return ok;
}

// parent = attach 父容器（nullptr = 不挂载）；lp_root = 根 View 的
// LayoutParams 生成者（AOSP LayoutInflater: attachToRoot=false 且 root != null
// 时，根仍用 root.generateLayoutParams(attrs) 产 LP；此前直接丢 root 导致
// 根 LP 退化为兜底 LinearLayout.LayoutParams → 强类型父容器 CCE）。
bool InflateLayout(InflateContext& ctx, uint32_t layout_res_id, VObject* parent,
                   VObject* lp_root, VObject** out_root, LayoutOverrides* out_overrides);

// 事件流 -> View 树；parent != nullptr 时挂到 parent 下。
bool InflateEvents(InflateContext& ctx, const std::vector<session::XmlResEvent>& events,
                   VObject* parent, VObject* lp_root, VObject** out_root,
                   LayoutOverrides* out_overrides) {
  struct Frame {
    VObject* view = nullptr;
    bool container = false;
  };
  std::vector<Frame> stack;
  VObject* root = nullptr;
  LayoutOverrides root_ov;

  for (const auto& ev : events) {
    if (ev.type == session::XmlResEvent::kStartTag) {
      if (ev.name == "merge") {
        // AOSP LayoutInflater：<merge/> 只允许作为「带根容器的 attachToRoot」
        // 的根；不产生节点，子 View 直接挂到当前容器（含 <include> 场景）。
        VObject* container = parent;
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
          if (it->view != nullptr) {
            container = it->view;
            break;
          }
        }
        if (container == nullptr) {
          ctx.error = "inflate: <merge /> requires a valid ViewGroup root";
          ctx.state.ThrowNew("Ljava/lang/RuntimeException;",
                             "inflate: <merge /> requires a valid ViewGroup root");
          return false;
        }
        stack.push_back(Frame{container, false});
        continue;
      }
      if (ev.name == "include") {
        // <include layout="@layout/x"/>：嵌套布局文件挂到当前容器。
        uint32_t nested = 0;
        LayoutOverrides inc_ov;
        for (const auto& attr : ev.attrs) {
          std::string_view name = AttrName(attr);
          if (name == "layout") {
            ResolvedValue v;
            if (ResolveAttrValue(ctx, attr, &v) &&
                (v.type == kValueReference || v.type == kValueDynamicReference)) {
              nested = attr.value_data;
            } else {
              nested = attr.value_data;  // 直接引用 id
            }
          } else if (name == "layout_width") {
            ResolveSizeValue(ctx, attr, &inc_ov.width);
            inc_ov.any = true;
          } else if (name == "layout_height") {
            ResolveSizeValue(ctx, attr, &inc_ov.height);
            inc_ov.any = true;
          } else if (name == "layout_marginTop") {
            float px = 0;
            if (ResolveDimensionPx(ctx, attr, &px)) {
              inc_ov.margin_top = static_cast<int32_t>(std::lround(px));
              inc_ov.any = true;
            }
          } else if (name == "layout_marginBottom") {
            float px = 0;
            if (ResolveDimensionPx(ctx, attr, &px)) {
              inc_ov.margin_bottom = static_cast<int32_t>(std::lround(px));
              inc_ov.any = true;
            }
          } else if (name == "layout_marginLeft" || name == "layout_marginStart") {
            float px = 0;
            if (ResolveDimensionPx(ctx, attr, &px)) {
              inc_ov.margin_left = static_cast<int32_t>(std::lround(px));
              inc_ov.any = true;
            }
          } else if (name == "layout_marginRight" || name == "layout_marginEnd") {
            float px = 0;
            if (ResolveDimensionPx(ctx, attr, &px)) {
              inc_ov.margin_right = static_cast<int32_t>(std::lround(px));
              inc_ov.any = true;
            }
          }
        }
        // <include> 自身占一对 start/end 事件但不产生节点：压空帧配平 end。
        stack.push_back(Frame{nullptr, false});
        if (nested == 0) continue;
        VObject* container = parent;
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
          if (it->view != nullptr) {
            container = it->view;
            break;
          }
        }
        VObject* nested_root = nullptr;
        LayoutOverrides nested_ov;
        if (!InflateLayout(ctx, nested, container, container, &nested_root, &nested_ov))
          return false;
        if (nested_root != nullptr) {
          LayoutOverrides effective = inc_ov.any ? inc_ov : nested_ov;
          SetChildLayoutParams(ctx, container, nested_root, effective);
          if (root == nullptr && container == parent) root = nested_root;
        }
        continue;
      }

      bool constraint_approx = false;
      VObject* view = nullptr;
      {
        // AOSP createView 顺序：Factory2/Factory 优先（tag 替换面——TextView →
        // ZHTextView 等），工厂未处理再走类解析。
        VObject* container_for_factory = nullptr;
        if (!stack.empty()) {
          container_for_factory = stack.back().view;
        } else {
          container_for_factory = lp_root != nullptr ? lp_root : parent;
        }
        view = TryCreateViaFactory(ctx, container_for_factory, ev.name);
      }
      if (view == nullptr) {
        std::string descriptor = WidgetDescriptorForTag(ev.name, &constraint_approx);
        VClass* klass = nullptr;
        if (!descriptor.empty()) {
          klass = ctx.state.linker->FindClass(descriptor);
        } else {
          // AOSP 语义：优先实例化布局 XML 指定的真实类。ConstraintLayout 等
          // 容器只有在类确实不可用时才近似为 LinearLayout——否则 app 侧
          // findViewById 的强转（ConstraintLayout）会 CCE，且约束布局语义
          // 完全丢失（0dp/约束边距）。未知 tag 同样走通用容器承载。
          klass = FindGuestClass(ctx.state, ctx.activity, ev.name);
          if (klass == nullptr) {
            constraint_approx = true;
          }
        }
        if (klass == nullptr) klass = ctx.state.linker->FindClass("Landroid/widget/LinearLayout;");
        view = NewView(ctx, klass);
      }
      if (view == nullptr) {
        ctx.error = "inflate: cannot instantiate " + ev.name;
        return false;
      }
      if (constraint_approx) SetIntFieldByName(view, "mOrientation", 1);  // VERTICAL

      // ---- 属性映射 ----
      LayoutOverrides ov;
      for (const auto& attr : ev.attrs) {
        std::string_view name = AttrName(attr);
        if (name.empty()) continue;
        if (name == "id") {
          // android:id 的 typed data 即 id 资源 id（TYPE_REFERENCE），不解析链
          // （id 资源无值语义；AOSP View.mID 存资源 id）。
          SetIntFieldByName(view, "mId", static_cast<int32_t>(attr.value_data));
        } else if (name == "text") {
          std::string s;
          if (ResolveStringValue(ctx, attr, &s)) {
            if (VField* f = FindFieldInChain(view->klass, "mText")) {
              SetRefField(view, f->offset,
                          reinterpret_cast<ObjHeader*>(
                              ctx.state.linker->AllocString(Utf8ToUtf16(s))));
            }
          }
        } else if (name == "textSize") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) SetFloatFieldByName(view, "mTextSize", px);
        } else if (name == "textColor") {
          int32_t color = 0;
          if (ResolveColorValue(ctx, attr, &color)) SetIntFieldByName(view, "mTextColor", color);
        } else if (name == "background") {
          int32_t color = 0;
          if (ResolveColorValue(ctx, attr, &color)) {
            SetIntFieldByName(view, "mBackgroundColor", color);
          }
        } else if (name == "padding") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) {
            int32_t p = static_cast<int32_t>(std::lround(px));
            SetIntFieldByName(view, "mPaddingLeft", p);
            SetIntFieldByName(view, "mPaddingTop", p);
            SetIntFieldByName(view, "mPaddingRight", p);
            SetIntFieldByName(view, "mPaddingBottom", p);
          }
        } else if (name == "paddingLeft" || name == "paddingStart") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) {
            SetIntFieldByName(view, "mPaddingLeft", static_cast<int32_t>(std::lround(px)));
          }
        } else if (name == "paddingTop") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) {
            SetIntFieldByName(view, "mPaddingTop", static_cast<int32_t>(std::lround(px)));
          }
        } else if (name == "paddingRight" || name == "paddingEnd") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) {
            SetIntFieldByName(view, "mPaddingRight", static_cast<int32_t>(std::lround(px)));
          }
        } else if (name == "paddingBottom") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) {
            SetIntFieldByName(view, "mPaddingBottom", static_cast<int32_t>(std::lround(px)));
          }
        } else if (name == "gravity") {
          int32_t g = ResolveIntValue(ctx, attr, 0);
          SetIntFieldByName(view, "mGravity", g);
        } else if (name == "orientation") {
          int32_t orent = ResolveIntValue(ctx, attr, 1);
          SetIntFieldByName(view, "mOrientation", orent);
        } else if (name == "visibility") {
          // XML 枚举 0/1/2（visible/invisible/gone）-> View 常量 0/4/8
          // （AOSP View 构造器同映射）
          int32_t vis = ResolveIntValue(ctx, attr, 0);
          if (vis == 1) vis = 4;
          else if (vis == 2) vis = 8;
          SetIntFieldByName(view, "mVisibility", vis);
        } else if (name == "checked") {
          int32_t checked = ResolveIntValue(ctx, attr, 0);
          SetIntFieldByName(view, "mChecked", checked);
        } else if (name == "progress") {
          int32_t progress = ResolveIntValue(ctx, attr, 0);
          SetIntFieldByName(view, "mProgress", progress);
        } else if (name == "layout_width") {
          ResolveSizeValue(ctx, attr, &ov.width);
          ov.any = true;
        } else if (name == "layout_height") {
          ResolveSizeValue(ctx, attr, &ov.height);
          ov.any = true;
        } else if (name == "layout_marginTop") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) {
            ov.margin_top = static_cast<int32_t>(std::lround(px));
            ov.any = true;
          }
        } else if (name == "layout_marginBottom") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) {
            ov.margin_bottom = static_cast<int32_t>(std::lround(px));
            ov.any = true;
          }
        } else if (name == "layout_marginLeft" || name == "layout_marginStart") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) {
            ov.margin_left = static_cast<int32_t>(std::lround(px));
            ov.any = true;
          }
        } else if (name == "layout_marginRight" || name == "layout_marginEnd") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) {
            ov.margin_right = static_cast<int32_t>(std::lround(px));
            ov.any = true;
          }
        } else if (name == "layout_margin") {
          float px = 0;
          if (ResolveDimensionPx(ctx, attr, &px)) {
            int32_t m = static_cast<int32_t>(std::lround(px));
            ov.margin_left = ov.margin_top = ov.margin_right = ov.margin_bottom = m;
            ov.any = true;
          }
        }
        // 其余（app:layout_constraint*、style、src、scaleType…）不解释
      }
      SetChildLayoutParams(
          ctx, stack.empty() ? (lp_root != nullptr ? lp_root : parent) : stack.back().view, view,
          ov);

      if (stack.empty()) {
        if (parent != nullptr) AddChild(ctx, parent, view);
        root = view;
        root_ov = ov;
      } else {
        AddChild(ctx, stack.back().view, view);
      }
      stack.push_back(Frame{view, IsViewGroupClass(view->klass)});
    } else if (ev.type == session::XmlResEvent::kEndTag) {
      if (!stack.empty()) stack.pop_back();
    } else if (ev.type == session::XmlResEvent::kEndDocument) {
      break;
    }
  }

  if (out_root != nullptr) *out_root = root;
  if (out_overrides != nullptr) *out_overrides = root_ov;
  return true;
}

// 读 layout 资源文件 + 解析 + 膨胀（root 挂 parent；根 LP 由 lp_root 生成）。
bool InflateLayout(InflateContext& ctx, uint32_t layout_res_id, VObject* parent,
                   VObject* lp_root, VObject** out_root, LayoutOverrides* out_overrides) {
  const ResourceSeam* seam = Seam();
  if (seam->open_resource_file == nullptr || seam->parse_xml_events == nullptr) {
    ctx.error = "inflate: resource seam not installed";
    return false;
  }
  std::string entry_path;
  std::vector<uint8_t> bytes;
  std::string err;
  if (!seam->open_resource_file(ctx.apk_path, layout_res_id, &entry_path, &bytes, &err)) {
    ctx.error = "inflate: open layout 0x" + [&] {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%08x", layout_res_id);
      return std::string(buf);
    }() + " failed: " + err;
    return false;
  }
  std::vector<session::XmlResEvent> events;
  if (!seam->parse_xml_events(bytes, &events, &err)) {
    ctx.error = "inflate: parse " + entry_path + " failed: " + err;
    return false;
  }
  if (!InflateEvents(ctx, events, parent, lp_root, out_root, out_overrides)) return false;
  if (out_root != nullptr && *out_root == nullptr) {
    // <merge/> 根：attach 语义下返回容器本身（AOSP inflate(merge, root, true)）
    if (parent != nullptr) {
      *out_root = parent;
    } else {
      ctx.error = "inflate: " + entry_path + " has no root element";
      return false;
    }
  }
  return true;
}

VObject* ActivityContentView(VObject* activity) {
  if (activity == nullptr) return nullptr;
  if (VField* f = FindFieldInChain(activity->klass, "mContentView")) {
    return reinterpret_cast<VObject*>(GetRefField(activity, f->offset));
  }
  return nullptr;
}

// Activity.addContentView 登记表（AOSP decor content parent 覆盖层面）：
// 首帧管线 traversal 后按窗口原点重新 measure/layout（覆盖在内容之上，
// 与 AOSP addContentView 落 decor 左上角语义同形）。按 Activity 键，VM 复位清空。
std::map<VObject*, std::vector<VObject*>> g_added_content_views;
// Context -> 平台 LayoutInflater 单例（AOSP ContextImpl 缓存；ResetInflateState 清）
std::map<VObject*, VObject*> g_layout_inflaters;

void ThrowInflateError(VMState& state, const std::string& message, bool* ok) {
  state.ThrowNew("Ljava/lang/RuntimeException;", message);
  *ok = false;
}

}  // namespace

// ---- 公开面（首帧管线/注册复位）----

void ResetInflateState() {
  g_added_content_views.clear();
  g_layout_inflaters.clear();
}

// 平台 LayoutInflater 单例（Context 缓存；AOSP ContextImpl.mLayoutInflater 同形）。
// 类 = lib VmLayoutInflater（extend lib LayoutInflater；inflate 委派 VmLayoutHost）。
VObject* AllocPlatformLayoutInflater(VMState& state, VObject* context) {
  auto it = g_layout_inflaters.find(context);
  if (it != g_layout_inflaters.end()) return it->second;
  VClass* klass = state.linker->FindClass("Landroid/view/VmLayoutInflater;");
  if (klass == nullptr) return nullptr;
  if (!state.linker->InitializeClass(klass, &state)) {
    state.ClearException();
    return nullptr;
  }
  VObject* inflater = state.linker->AllocObject(klass);
  if (inflater == nullptr) return nullptr;
  for (auto& m : klass->direct_methods) {
    if (m.name != "<init>") continue;
    if (m.param_types.size() != 1 ||
        m.param_types[0] != "Landroid/content/Context;") {
      continue;
    }
    Value args[2] = {};
    args[0].ref = inflater;
    args[1].ref = context;
    Value ret{};
    if (!InvokeMethod(state, &m, args, 2, &ret)) {
      state.ClearException();
      return nullptr;
    }
    break;
  }
  g_layout_inflaters[context] = inflater;
  if (state.heap != nullptr) state.heap->Pin(inflater);
  return inflater;
}

// VmLayoutHost.inflate(LayoutInflater, int, ViewGroup, boolean)：资源布局
// 引擎入口（lib VmLayoutInflater 委派；attachToRoot 语义 = AOSP 返回 root）。
Value VmLayoutHostInflate(VMState& state, VObject* self, const Value* args, size_t n,
                          bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 4) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "LayoutInflater.inflate args");
    *ok = false;
    return r;
  }
  VObject* inflater = reinterpret_cast<VObject*>(args[0].ref);
  uint32_t res_id = static_cast<uint32_t>(args[1].i);
  VObject* root = reinterpret_cast<VObject*>(args[2].ref);
  bool attach = args[3].i != 0;
  VObject* context = nullptr;
  if (inflater != nullptr) {
    if (VField* f = FindFieldInChain(inflater->klass, "mContext")) {
      context = reinterpret_cast<VObject*>(GetRefField(inflater, f->offset));
    }
  }
  InflateContext ctx{state, context, ResolveApkPath(state.linker, context), {}, inflater};
  if (ctx.apk_path.empty()) {
    ThrowInflateError(state, "LayoutInflater.inflate: no APK resource path", ok);
    return r;
  }
  VObject* inflated = nullptr;
  LayoutOverrides ov;
  if (!InflateLayout(ctx, res_id, attach ? root : nullptr, root, &inflated, &ov) ||
      inflated == nullptr) {
    ThrowInflateError(state, "LayoutInflater.inflate(0x" + [&] {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%x", res_id);
      return std::string(buf);
    }() + "): " + (ctx.error.empty() ? "inflate failed" : ctx.error), ok);
    return r;
  }
#if defined(__ANDROID__) && DEXVM_INFLATE_DUMP
  {
    struct Dump {
      static void walk(VObject* v, int depth) {
        if (v == nullptr || depth > 8) return;
        int32_t id = 0;
        if (VField* f = FindFieldInChain(v->klass, "mId")) {
          id = GetIField(v, f->offset);
        }
        __android_log_print(ANDROID_LOG_INFO, "DexVmInflate", "%*s%s id=0x%08x", depth * 2,
                            "", v->klass != nullptr ? v->klass->descriptor.c_str() : "?", id);
        if (VField* cf = FindFieldInChain(v->klass, "mChildren")) {
          VArray* arr = reinterpret_cast<VArray*>(GetRefField(v, cf->offset));
          if (arr != nullptr) {
            for (int32_t i = 0; i < arr->length; i++) {
              walk(reinterpret_cast<VObject*>(GetArrRef(arr, i)), depth + 1);
            }
          }
        }
      }
    };
    __android_log_print(ANDROID_LOG_INFO, "DexVmInflate", "inflate 0x%08x attach=%d", res_id,
                        attach ? 1 : 0);
    Dump::walk(inflated, 0);
  }
#endif
  if (attach && root != nullptr) {
    r.ref = args[2].ref;  // AOSP attachToRoot=true 返回 root
    return r;
  }
  r.ref = reinterpret_cast<ObjHeader*>(inflated);
  return r;
}

std::vector<VObject*> AddedContentViews(VObject* activity) {
  auto it = g_added_content_views.find(activity);
  return it == g_added_content_views.end() ? std::vector<VObject*>() : it->second;
}

// ---- intrinsic 落点 ----

// Activity.setContentView(int)：资源布局 -> guest View 树 -> mContentView。
Value ActSetContentViewInt(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  *ok = true;
  if (n < 1) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "setContentView(layoutRes) missing id");
    *ok = false;
    return Value{};
  }
  InflateContext ctx{state, self, ResolveApkPath(state.linker, self), {}};
  if (ctx.apk_path.empty()) {
    ThrowInflateError(state, "setContentView(" + std::to_string(args[0].i) +
                                 "): no APK resource path for guest activity", ok);
    return Value{};
  }
  VObject* root = nullptr;
  LayoutOverrides ov;
  if (!InflateLayout(ctx, static_cast<uint32_t>(args[0].i), nullptr, nullptr, &root, &ov) ||
      root == nullptr) {
    ThrowInflateError(state, "setContentView(0x" + [&] {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%x", args[0].i);
      return std::string(buf);
    }() + "): " + (ctx.error.empty() ? "inflate failed" : ctx.error), ok);
    return Value{};
  }
  // 语义落点：Activity.setContentView(View)（mContentView 字段缝）
  Value vargs[1] = {};
  vargs[0].ref = root;
  bool vok = true;
  ActSetContentView(state, self, vargs, 1, &vok);
  if (!vok) {
    *ok = false;
    return Value{};
  }
  return Value{};
}

// Activity.findViewById(int)：内容 View 树按 mId 查（AOSP getDecorView 子树同径）。
Value ActFindViewById(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  VObject* content = ActivityContentView(self);
  if (content == nullptr) return r;  // 尚未 setContentView = null（AOSP 语义）
  return ViewFindViewById(state, content, args, 1, ok);
}

// Activity.addContentView(View, LayoutParams)：追加到内容根容器（decor content
// parent 等价；无内容根时作为内容根）。
Value ActAddContentView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "addContentView view == null");
    *ok = false;
    return Value{};
  }
  VObject* view = reinterpret_cast<VObject*>(args[0].ref);
  if (n >= 2 && args[1].ref != nullptr) {
    if (VField* f = FindFieldInChain(view->klass, "mLayoutParams")) {
      SetRefField(view, f->offset, args[1].ref);
    }
  }
  VObject* content = ActivityContentView(self);
  if (content == nullptr) {
    Value vargs[1] = {};
    vargs[0].ref = args[0].ref;
    return ActSetContentView(state, self, vargs, 1, ok);
  }
  InflateContext add_ctx{state, self, {}, {}};
  if (!AddChild(add_ctx, content, view)) {
    // 内容根非 ViewGroup：AOSP 中 addContentView 落 decor 内容区，这里保持
    // 语义可观测（不静默丢失）——登记为失败诊断。
    state.ThrowNew("Ljava/lang/IllegalStateException;",
                   "addContentView: content view is not a ViewGroup");
    *ok = false;
    return Value{};
  }
  // 覆盖层面：首帧管线 traversal 后按窗口原点重新 measure/layout（decor
  // content parent 叠放语义；见 AddedContentViews）。
  g_added_content_views[self].push_back(view);
  return Value{};
}

}  // namespace dexvm::runtime
