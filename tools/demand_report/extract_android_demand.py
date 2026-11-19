#!/usr/bin/env python3
# extract_android_demand.py —— 知乎考卷 android.* 需求面静态提取（framework 面批量补充轮）。
#
# 扫 APK 全部 dex（zhihu.apk 18 个）：
#   - type_id / method_id / field_id 归属 + 指令级引用频次（invoke/field-op/type-op/
#     catch-handler/const-string 类名）
#   - 「类 × 方法/字段 × 引用频次」排序清单
#   - 与现有面（builtin_classes.def + runtime-libs android/** + framework/builtins
#     C++ 注册面成员名）做差集 = 缺口清单（类级 + 成员级）
#   - 启动路径加权视图（application + manifest providers 的 APK 内类闭包）
#
# 输出（--out 目录，可复跑覆盖）：
#   report.txt          人读报告（摘要/家族排序/top 表/缺口清单/覆盖率）
#   demand_classes.tsv  类级需求全量（class  freq  startup_freq  dex  referrers  covered）
#   demand_members.tsv  成员级需求全量（class  member  kind  freq  startup_freq  covered）
#
# 用法: python3 -I tools/demand_report/extract_android_demand.py \
#         [--apk /tmp/target-apks/zhihu.apk] [--repo <android-vm>] [--out tools/demand_report] [--top 60]
# 依赖: python3 标准库 +（可选）aapt（启动路径 manifest providers 提取；缺则跳过该视图）。
import argparse
import io
import os
import re
import struct
import subprocess
import sys
import zipfile
from collections import defaultdict, deque

# ---------------------------------------------------------------------------
# dex 基础解析
# ---------------------------------------------------------------------------


class Dex:
    def __init__(self, data: bytes, name: str):
        self.data = data
        self.name = name
        if data[:4] != b"dex\n":
            raise ValueError(f"{name}: not a dex file")
        (
            self.string_ids_size,
            self.string_ids_off,
            self.type_ids_size,
            self.type_ids_off,
            self.proto_ids_size,
            self.proto_ids_off,
            self.field_ids_size,
            self.field_ids_off,
            self.method_ids_size,
            self.method_ids_off,
            self.class_defs_size,
            self.class_defs_off,
        ) = struct.unpack_from("<12I", data, 56)

    def u4(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def u2(self, off):
        return struct.unpack_from("<H", self.data, off)[0]

    def uleb(self, off):
        val = 0
        shift = 0
        while True:
            b = self.data[off]
            off += 1
            val |= (b & 0x7F) << shift
            if b & 0x80 == 0:
                break
            shift += 7
        return val, off

    def sleb(self, off):
        val = 0
        shift = 0
        while True:
            b = self.data[off]
            off += 1
            val |= (b & 0x7F) << shift
            shift += 7
            if b & 0x80 == 0:
                if b & 0x40:
                    val |= -(1 << shift)
                break
        return val, off

    def string(self, idx):
        off = self.u4(self.string_ids_off + 4 * idx)
        _utf16_size, off = self.uleb(off)
        end = self.data.index(b"\x00", off)
        raw = self.data[off:end]
        # mutf8 -> str（近似：按 utf-8 解，非法序列替换；对描述符/类名无影响）
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            return raw.decode("utf-8", "replace")

    def strings(self):
        if not hasattr(self, "_strings_cache"):
            self._strings_cache = [self.string(i) for i in range(self.string_ids_size)]
        return self._strings_cache

    def type_desc(self, idx):
        return self.strings()[self.u4(self.type_ids_off + 4 * idx)]

    def field_ref(self, idx):
        class_idx, type_idx, name_idx = struct.unpack_from(
            "<HHI", self.data, self.field_ids_off + 8 * idx
        )
        return self.type_desc(class_idx), self.type_desc(type_idx), self.strings()[name_idx]

    def method_ref(self, idx):
        class_idx, proto_idx, name_idx = struct.unpack_from(
            "<HHI", self.data, self.method_ids_off + 8 * idx
        )
        return self.type_desc(class_idx), self.strings()[name_idx], proto_idx

    def proto_desc(self, idx):
        shorty_idx, ret_idx, params_off = struct.unpack_from(
            "<III", self.data, self.proto_ids_off + 12 * idx
        )
        ret = self.type_desc(ret_idx)
        params = []
        if params_off != 0:
            size = self.u4(params_off)
            for i in range(size):
                params.append(self.type_desc(self.u2(params_off + 4 + 2 * i)))
        return params, ret


# 指令宽度表（16-bit code units；dalvik 指令全集，payload 另判）
# 00 nop | 01-09 move* 1/2/3 | 0a-11 move-result*/return* 1 | 12 const/4 1
# 13 const/16 2 | 14 const 3 | 15 const/high16 2 | 16 const-wide/16 2
# 17 const-wide/32 3 | 18 const-wide 5 | 19 const-wide/high16 2
# 1a const-string 2 | 1b const-string/jumbo 3 | 1c const-class 2
# 1d monitor-enter 1 | 1e monitor-exit 1 | 1f check-cast 2 | 20 instance-of 2
# 21 new-instance 2 | 22 new-array 2 | 23 filled-new-array 3
# 24 filled-new-array/range 3 | 25 fill-array-data 3 | 26 throw 1
# 27 goto 1 | 28 goto/16 2 | 29 goto/32 3 | 2a packed-switch 3 | 2b sparse-switch 3
# 2c-30 cmp* 2 | 31-36 if-* 2 | 37-3c if-*z 2 | 3d-43 unused
# 44-51 aget/aput* 2 | 52-5f iget/iput* 2 | 60-6d sget/sput* 2
# 6e-72 invoke-* 3 | 73 unused | 74-78 invoke-*/range 3 | 79-7a unused
# 7b-8f unop 1 | 90-af binop/2addr 1 | b0-cf binop/16 2
# d0-d7 binop/lit16 2 | d8-e2 binop/lit8 2
OP_WIDTH = [1] * 256
OP_WIDTH[0x02] = 2
OP_WIDTH[0x03] = 3
OP_WIDTH[0x05] = 2
OP_WIDTH[0x06] = 3
OP_WIDTH[0x08] = 2
OP_WIDTH[0x09] = 3
OP_WIDTH[0x13] = 2
OP_WIDTH[0x14] = 3
OP_WIDTH[0x15] = 2
OP_WIDTH[0x16] = 2
OP_WIDTH[0x17] = 3
OP_WIDTH[0x18] = 5
OP_WIDTH[0x19] = 2
OP_WIDTH[0x1A] = 2
OP_WIDTH[0x1B] = 3
OP_WIDTH[0x1C] = 2
OP_WIDTH[0x1F] = 2
OP_WIDTH[0x20] = 2
OP_WIDTH[0x21] = 2
OP_WIDTH[0x22] = 2
OP_WIDTH[0x23] = 3
OP_WIDTH[0x24] = 3
OP_WIDTH[0x25] = 3
OP_WIDTH[0x28] = 2
OP_WIDTH[0x29] = 3
OP_WIDTH[0x2A] = 3
OP_WIDTH[0x2B] = 3
for op in range(0x2C, 0x3D):
    OP_WIDTH[op] = 2
for op in range(0x44, 0x6E):
    OP_WIDTH[op] = 2
for op in range(0x6E, 0x73):
    OP_WIDTH[op] = 3
for op in range(0x74, 0x79):
    OP_WIDTH[op] = 3
for op in range(0xB0, 0xD0):
    OP_WIDTH[op] = 2
for op in range(0xD0, 0xE3):
    OP_WIDTH[op] = 2

# 引用指令 opcode 分类
INVOKE_OPS = set(range(0x6E, 0x73)) | set(range(0x74, 0x79))
IGET_IPUT_OPS = set(range(0x52, 0x60))
SGET_SPUT_OPS = set(range(0x60, 0x6E))
TYPE_OPS_21C = {0x1C, 0x1F, 0x21}  # const-class, check-cast, new-instance
TYPE_OPS_22C = {0x20, 0x22}  # instance-of, new-array
TYPE_OPS_35C = {0x23}  # filled-new-array
TYPE_OPS_3RC = {0x24}  # filled-new-array/range
CONST_STRING_OPS = {0x1A, 0x1B}


def extract_code_refs(dex: Dex, insns_off, insns_units, refs):
    """扫描 code_item 指令区 + catch handlers，累加引用计数到 refs。"""
    data = dex.data
    base = insns_off
    end = insns_off + insns_units * 2
    off = base
    while off < end:
        unit = dex.u2(off)
        op = unit & 0xFF
        if op == 0x00 and unit != 0x00:
            ident = unit
            if ident == 0x0100:  # packed-switch payload: 8 + 4*size bytes
                size = dex.u2(off + 2)
                off += 8 + 4 * size
                continue
            if ident == 0x0200:  # sparse-switch payload: 4 + 8*size bytes
                size = dex.u2(off + 2)
                off += 4 + 8 * size
                continue
            if ident == 0x0300:  # fill-array-data payload
                elem_w = dex.u2(off + 2)
                size = dex.u4(off + 4)
                nbytes = elem_w * size
                off += 8 + nbytes + (nbytes & 1)
                continue
        w = OP_WIDTH[op]
        if op in INVOKE_OPS:
            idx = dex.u2(off + 2)
            if idx < dex.method_ids_size:
                refs["method"].add((idx, off))
        elif op in IGET_IPUT_OPS or op in SGET_SPUT_OPS:
            idx = dex.u2(off + 2)
            if idx < dex.field_ids_size:
                refs["field"].add((idx, off))
        elif op in TYPE_OPS_21C or op in TYPE_OPS_22C:
            idx = dex.u2(off + 2)
            if idx < dex.type_ids_size:
                refs["type"].add((idx, off))
        elif op in TYPE_OPS_35C or op in TYPE_OPS_3RC:
            idx = dex.u2(off + 2)
            if idx < dex.type_ids_size:
                refs["type"].add((idx, off))
        elif op in CONST_STRING_OPS:
            idx = dex.u4(off + 2) if op == 0x1B else dex.u2(off + 2)
            if idx < dex.string_ids_size:
                refs["string"].add((idx, off))
        off += w * 2


# ---------------------------------------------------------------------------
# 需求聚合
# ---------------------------------------------------------------------------


def norm_type(desc):
    """[Landroid/x; / Landroid/x; -> android.x；其它返回 None。"""
    d = desc
    while d.startswith("["):
        d = d[1:]
    if not (d.startswith("Landroid/") or d.startswith("Landroidx/")):
        return None
    if not d.endswith(";"):
        return None
    return d[1:-1].replace("/", ".")


def class_to_desc(cls):
    return "L" + cls.replace(".", "/") + ";"


class Demand:
    def __init__(self):
        # android.* 类级
        self.class_freq = defaultdict(int)  # cls -> 引用次数（成员引用 + 类型指令 + catch + supertype）
        self.class_startup_freq = defaultdict(int)
        self.class_dexes = defaultdict(set)
        self.class_referrers = defaultdict(set)  # cls -> set(referrer_class)
        # 成员级：cls -> {"m:name/arity"|"f:name": freq}
        self.member_freq = defaultdict(lambda: defaultdict(int))
        self.member_startup_freq = defaultdict(lambda: defaultdict(int))
        self.member_referrers = defaultdict(lambda: defaultdict(set))
        # 全量成员签名（便于缺口精确比对）: cls -> set("name(arity)")
        self.member_arities = defaultdict(dict)
        self.field_names = defaultdict(set)
        # 精确签名（dex proto 全形）：(cls, key) -> set("name(params)ret" / "f:name:type")
        self.member_sig = defaultdict(lambda: defaultdict(set))
        # 引用图（APK 内类）：referrer -> set(referenced apk classes)
        self.edges = defaultdict(set)
        self.apk_classes = set()
        self.apk_super = {}  # apk cls -> super cls（可能 android.*）
        self.apk_declared_types = defaultdict(set)  # apk cls -> 声明字段/签名中出现的类型
        self.const_string_hits = defaultdict(int)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk", default="/tmp/target-apks/zhihu.apk")
    ap.add_argument("--repo", default=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument("--out", default=None)
    ap.add_argument("--top", type=int, default=60)
    ap.add_argument("--startup-cap", type=int, default=30000)
    args = ap.parse_args()
    out_dir = args.out or os.path.join(args.repo, "tools", "demand_report")
    os.makedirs(out_dir, exist_ok=True)

    dem = Demand()
    dex_names = []

    with zipfile.ZipFile(args.apk) as zf:
        dex_names = sorted(
            (n for n in zf.namelist() if re.fullmatch(r"classes\d*\.dex", n)),
            key=lambda n: (len(n), n),
        )
        for dn in dex_names:
            dex = Dex(zf.read(dn), dn)
            strs = dex.strings()
            # ---- class_defs：APK 类集 + 超类/接口 + 声明面 ----
            class_infos = []  # (cls_name, class_data_off)
            for i in range(dex.class_defs_size):
                base = dex.class_defs_off + 32 * i
                class_idx, access, superclass_idx, interfaces_off, _src, _ann, class_data_off, _staticv = struct.unpack_from(
                    "<8I", dex.data, base
                )
                cls_desc = dex.type_desc(class_idx)
                cls = norm_type(cls_desc)
                if cls is None and not cls_desc.startswith("L"):
                    continue
                cls_name = cls or cls_desc[1:-1].replace("/", ".")
                dem.apk_classes.add(cls_name)
                if superclass_idx != 0xFFFFFFFF:
                    sup = norm_type(dex.type_desc(superclass_idx))
                    if sup:
                        dem.apk_super[cls_name] = sup
                        dem.edges[cls_name].add(sup)
                    else:
                        sd = dex.type_desc(superclass_idx)
                        if sd.startswith("L"):
                            dem.apk_super[cls_name] = sd[1:-1].replace("/", ".")
                if interfaces_off != 0:
                    n = dex.u4(interfaces_off)
                    for j in range(n):
                        it = norm_type(dex.type_desc(dex.u2(interfaces_off + 4 + 2 * j)))
                        if it:
                            dem.edges[cls_name].add(it)
                class_infos.append((cls_name, class_data_off))

            # ---- class_data：声明字段/方法 + 每方法 code 引用 ----
            for cls_name, cdo in class_infos:
                if cdo == 0:
                    continue
                off = cdo
                static_fields_size, off = dex.uleb(off)
                instance_fields_size, off = dex.uleb(off)
                direct_methods_size, off = dex.uleb(off)
                virtual_methods_size, off = dex.uleb(off)
                for _ in range(static_fields_size + instance_fields_size):
                    fi, off = dex.uleb(off)
                    _af, off = dex.uleb(off)
                    f_cls, f_type, f_name = dex.field_ref(fi)
                    t = norm_type(f_type)
                    if t:
                        dem.apk_declared_types[cls_name].add(t)
                method_entries = []
                for _ in range(direct_methods_size + virtual_methods_size):
                    mi, off = dex.uleb(off)
                    af, off = dex.uleb(off)
                    code_off, off = dex.uleb(off)
                    method_entries.append((mi, code_off))
                for mi, code_off in method_entries:
                    m_cls, m_name, proto_idx = dex.method_ref(mi)
                    params, ret = dex.proto_desc(proto_idx)
                    for t0 in params + [ret]:
                        t = norm_type(t0)
                        if t:
                            dem.apk_declared_types[cls_name].add(t)
                    if code_off == 0:
                        continue
                    # code_item header: 4x u2 + 2x u4（reg/ins/outs/tries/debug/insns_size）
                    _reg, _ins, _outs, tries_size, _dbg, insns_size = struct.unpack_from(
                        "<4H2I", dex.data, code_off
                    )
                    insns_off = code_off + 16
                    refs = {"method": set(), "field": set(), "type": set(), "string": set()}
                    extract_code_refs(dex, insns_off, insns_size, refs)
                    # catch handler 类型
                    if tries_size > 0:
                        tries_off = insns_off + insns_size * 2 + (insns_size & 1) * 2
                        handlers_off = tries_off + tries_size * 8
                        handlers_size, hoff = dex.uleb(handlers_off)
                        for _ in range(handlers_size):
                            size, hoff = dex.sleb(hoff)
                            cnt = abs(size)
                            for _ in range(cnt):
                                _ti, hoff = dex.uleb(hoff)
                            for _ in range(cnt):
                                _addr, hoff = dex.uleb(hoff)
                            if size <= 0:
                                _all, hoff = dex.uleb(hoff)
                    # ---- 归因 ----
                    def bump_type(desc, count=1):
                        t = norm_type(desc)
                        if t is None:
                            return None
                        dem.class_freq[t] += count
                        dem.class_dexes[t].add(dex.name)
                        dem.class_referrers[t].add(cls_name)
                        return t

                    for idx, _o in refs["method"]:
                        f_cls, f_name, proto_idx = dex.method_ref(idx)
                        params, ret = dex.proto_desc(proto_idx)
                        arity = len(params)
                        for t0 in params + [ret]:
                            bump_type(t0)
                        owner = norm_type(f_cls)
                        if owner:
                            dem.class_freq[owner] += 1
                            dem.class_dexes[owner].add(dex.name)
                            dem.class_referrers[owner].add(cls_name)
                            key = f"{f_name}({arity})"
                            dem.member_freq[owner][key] += 1
                            dem.member_referrers[owner][key].add(cls_name)
                            dem.member_arities[owner].setdefault(key, set()).add(f_name)
                            sig = f"{f_name}({''.join(params)}){ret}"
                            dem.member_sig[owner][key].add(sig)
                            if owner in dem.apk_classes:
                                dem.edges[cls_name].add(owner)
                    for idx, _o in refs["field"]:
                        f_cls, f_type, f_name = dex.field_ref(idx)
                        bump_type(f_type)
                        owner = norm_type(f_cls)
                        if owner:
                            dem.class_freq[owner] += 1
                            dem.class_dexes[owner].add(dex.name)
                            dem.class_referrers[owner].add(cls_name)
                            dem.member_freq[owner][f"f:{f_name}"] += 1
                            dem.member_referrers[owner][f"f:{f_name}"].add(cls_name)
                            dem.field_names[owner].add(f_name)
                            dem.member_sig[owner][f"f:{f_name}"].add(f"f:{f_name}:{f_type}")
                            if owner in dem.apk_classes:
                                dem.edges[cls_name].add(owner)
                    for idx, _o in refs["type"]:
                        desc = dex.type_desc(idx)
                        t = bump_type(desc)
                        if t and t in dem.apk_classes:
                            dem.edges[cls_name].add(t)
                    for idx, _o in refs["string"]:
                        s = strs[idx]
                        if s.startswith("Landroid/") or s.startswith("android."):
                            dem.const_string_hits[s] += 1
            print(f"[demand] scanned {dn}: apk_classes={len(dem.apk_classes)}", file=sys.stderr)

    # ---- 启动路径闭包 ----
    startup_seeds = set()
    manifest_note = ""
    try:
        aapt = None
        for cand in (
            os.path.expanduser("~/Library/Android/sdk/build-tools/36.0.0/aapt"),
            "aapt",
        ):
            if os.path.exists(cand) or cand == "aapt":
                aapt = cand
                break
        if aapt:
            xml = subprocess.run(
                [aapt, "dump", "xmltree", args.apk, "AndroidManifest.xml"],
                capture_output=True, text=True, timeout=60,
            ).stdout
            cur_kind = None
            for line in xml.splitlines():
                km = re.search(r"E: (provider|application)\b", line)
                if km:
                    cur_kind = km.group(1)
                    continue
                nm = re.search(r'A: android:name\(0x01010003\).*?"([^"]+)"', line)
                if nm and cur_kind in ("provider", "application"):
                    raw = nm.group(1).replace("/", ".")
                    if raw.startswith("."):
                        raw = "com.zhihu.android" + raw
                    elif "." not in raw:
                        raw = "com.zhihu.android." + raw
                    startup_seeds.add(raw)
                    cur_kind = None
            manifest_note = f"manifest seeds extracted via aapt: {len(startup_seeds)}"
        else:
            manifest_note = "aapt not found; startup view skipped"
    except Exception as e:  # -fno-exceptions 风格：读不到就降级
        manifest_note = f"manifest parse failed ({e}); startup view skipped"

    startup_set = set()
    if startup_seeds:
        q = deque()
        for s in startup_seeds:
            # 解析前缀形式 com.zhihu.android.app.ZhihuApplication 等已在解析中补全
            for cand in (s, s.lstrip(".")):
                if cand in dem.apk_classes:
                    q.append(cand)
                    startup_set.add(cand)
        while q and len(startup_set) < args.startup_cap:
            cur = q.popleft()
            for nxt in dem.edges.get(cur, ()):
                if nxt not in startup_set and nxt in dem.apk_classes:
                    startup_set.add(nxt)
                    q.append(nxt)
            for t in dem.apk_declared_types.get(cur, ()):
                if t in dem.apk_classes and t not in startup_set:
                    startup_set.add(t)
                    q.append(t)

    # startup freq：闭包内类的引用计数
    with zipfile.ZipFile(args.apk) as zf:
        for dn in dex_names:
            dex = Dex(zf.read(dn), dn)
            for i in range(dex.class_defs_size):
                base = dex.class_defs_off + 32 * i
                class_idx, _a, _s, _i, _src, _ann, class_data_off, _sv = struct.unpack_from(
                    "<8I", dex.data, base
                )
                cls_desc = dex.type_desc(class_idx)
                cls = norm_type(cls_desc) or (cls_desc[1:-1].replace("/", ".") if cls_desc.startswith("L") else None)
                if cls is None or cls not in startup_set or class_data_off == 0:
                    continue
                off = class_data_off
                sfs, off = dex.uleb(off)
                ifs, off = dex.uleb(off)
                dms, off = dex.uleb(off)
                vms, off = dex.uleb(off)
                for _ in range(sfs + ifs):
                    _fi, off = dex.uleb(off)
                    _af, off = dex.uleb(off)
                for _ in range(dms + vms):
                    mi, off = dex.uleb(off)
                    _af, off = dex.uleb(off)
                    code_off, off = dex.uleb(off)
                    if code_off == 0:
                        continue
                    insns_size = struct.unpack_from("<I", dex.data, code_off + 12)[0]
                    insns_off = code_off + 16
                    refs = {"method": set(), "field": set(), "type": set(), "string": set()}
                    extract_code_refs(dex, insns_off, insns_size, refs)
                    for idx, _o in refs["method"]:
                        f_cls, f_name, proto_idx = dex.method_ref(idx)
                        owner = norm_type(f_cls)
                        if owner:
                            dem.class_startup_freq[owner] += 1
                            params, ret = dex.proto_desc(proto_idx)
                            dem.member_startup_freq[owner][f"{f_name}({len(params)})"] += 1
                        for t0 in (dex.proto_desc(proto_idx)[0] + [dex.proto_desc(proto_idx)[1]]):
                            t = norm_type(t0)
                            if t:
                                dem.class_startup_freq[t] += 1
                    for idx, _o in refs["field"]:
                        f_cls, f_type, f_name = dex.field_ref(idx)
                        owner = norm_type(f_cls)
                        if owner:
                            dem.class_startup_freq[owner] += 1
                            dem.member_startup_freq[owner][f"f:{f_name}"] += 1
                        t = norm_type(f_type)
                        if t:
                            dem.class_startup_freq[t] += 1
                    for idx, _o in refs["type"]:
                        t = norm_type(dex.type_desc(idx))
                        if t:
                            dem.class_startup_freq[t] += 1

    # ---- 现有覆盖面 ----
    repo = args.repo
    covered_classes = set()  # 规范名 android.text.TextPaint
    covered_members = defaultdict(set)  # cls -> {"name" | "name(arity)" | "f:name"}
    covered_src = {}  # cls -> "builtin"|"lib"

    def_desc = os.path.join(repo, "app/src/main/cpp/core/builtin_classes.def")
    with open(def_desc) as f:
        for line in f:
            m = re.search(r'DEXVM_(?:CORE|FRAMEWORK)_BUILTIN_CLASS\("([^"]+)"\)', line)
            if m:
                d = m.group(1)
                if d.startswith("L"):
                    n = d[1:-1].replace("/", ".")
                    covered_classes.add(n)
                    covered_src[n] = "builtin"

    # lib 源
    lib_root = os.path.join(repo, "runtime-libs/src/main/java")
    # 编不过跳过集（构建期记录；不可当作覆盖）
    skipped_sources = set()
    uncompilable = os.path.join(repo, "runtime-libs/build/runtime-packaging/uncompilable.txt")
    if os.path.exists(uncompilable):
        for line in open(uncompilable):
            line = line.strip()
            if line and not line.startswith("#"):
                skipped_sources.add(line)
    # 权威类级覆盖：runtime.dex class_defs（存在时优先；嵌套类/编译产物全收）
    runtime_dex_dir = os.path.join(repo, "runtime-libs/build/runtime-dex")
    for dfile in sorted(os.listdir(runtime_dex_dir)) if os.path.isdir(runtime_dex_dir) else []:
        if not dfile.endswith(".dex"):
            continue
        try:
            with open(os.path.join(runtime_dex_dir, dfile), "rb") as fh:
                rdex = Dex(fh.read(), dfile)
            for i in range(rdex.class_defs_size):
                base = rdex.class_defs_off + 32 * i
                class_idx = rdex.u4(base)
                d = rdex.type_desc(class_idx)
                if d.startswith("L") and d.endswith(";"):
                    n = d[1:-1].replace("/", ".")
                    covered_classes.add(n)
                    covered_src.setdefault(n, "lib")
        except Exception:
            pass
    for dirpath, _dirs, files in os.walk(lib_root):
        for fn in files:
            if not fn.endswith(".java"):
                continue
            path = os.path.join(dirpath, fn)
            rel_path = os.path.relpath(path, lib_root).replace(os.sep, "/")
            if rel_path in skipped_sources:
                continue  # 编不过跳过 = 不在 runtime.dex = 不算覆盖
            rel = rel_path[: -len(".java")]
            cls = rel.replace("/", ".")
            covered_classes.add(cls)
            covered_src.setdefault(cls, "lib")
            src = open(path, encoding="utf-8", errors="replace").read()
            # 嵌套 class/interface/enum 声明 -> Outer$Name 覆盖
            nested_names = []
            for nm in re.finditer(
                r"(?:(?:public|protected|private|static|final|abstract|strictfp)\s+)*"
                r"(?:class|interface|enum)\s+(\w+)\b", src,
            ):
                inner = nm.group(1)
                if inner != cls.split(".")[-1]:
                    nested_names.append(cls + "$" + inner)
                    covered_classes.add(cls + "$" + inner)
                    covered_src.setdefault(cls + "$" + inner, "lib")
            for m in re.finditer(
                r"(?:(?:public|protected|private|static|final|synchronized|native|abstract|strictfp)\s+)+"
                r"[\w<>\[\],.?\s]+?\s+(\w+)\s*\(([^)]*)\)\s*(?:throws[\w.,\s]+)?\s*[{;]",
                src,
            ):
                name, params = m.group(1), m.group(2)
                if name in ("if", "for", "while", "switch", "catch", "return", "new", "synchronized"):
                    continue
                arity = 0 if not params.strip() else params.count(",") + 1
                covered_members[cls].add(name)
                covered_members[cls].add(f"{name}({arity})")
            for m in re.finditer(r"\b(\w+)\s*\(\s*\)\s*\{\s*super\s*\(", src):
                covered_members[cls].add(m.group(1))
            for m in re.finditer(
                r"(?:(?:public|protected|private|static|final|transient|volatile)\s+)+"
                r"[\w<>\[\],.?\s]+?\s+(\w+)\s*(?:=[^;]+)?;", src,
            ):
                covered_members[cls].add(f"f:{m.group(1)}")
            # 文件内成员名镜像给嵌套类（同文件声明口径）
            for nested in nested_names:
                covered_members[nested] |= covered_members[cls]

    # C++ 注册面成员（AddIntrinsicMethod/AddMethodSkeleton/AddIntrinsic/AddInstanceField）
    cpp_root = os.path.join(repo, "app/src/main/cpp")
    var_to_desc = {}
    add_call = re.compile(
        r'\b(AddIntrinsicMethod|AddMethodSkeleton|AddIntrinsic|AddInstanceField|AddStaticField)\(\s*(\w+)\s*,\s*"([^"]+)"'
    )
    make_call = re.compile(r'\b(\w+)\s*=\s*(?:MakeClass\([^,]+,\s*[^,]+,\s*"([^"]+)"|[^;]*FindClass\("([^"]+)"\))')
    for dirpath, _dirs, files in os.walk(cpp_root):
        if "/tests/" in dirpath or "/out/" in dirpath:
            continue
        for fn in files:
            if not fn.endswith((".cc", ".h")):
                continue
            path = os.path.join(dirpath, fn)
            src = open(path, encoding="utf-8", errors="replace").read()
            for m in make_call.finditer(src):
                desc = m.group(2) or m.group(3) or ""
                if desc.startswith("L"):
                    var_to_desc[m.group(1)] = desc[1:-1].replace("/", ".")
            for m in add_call.finditer(src):
                kind, var, name = m.groups()
                desc = var_to_desc.get(var)
                if desc is None:
                    continue
                covered_classes.add(desc)
                covered_src.setdefault(desc, "builtin")
                covered_members[desc].add(f"f:{name}" if kind.endswith("Field") else name)

    # ---- 差集 ----
    def is_android(cls):
        return cls.startswith("android.")

    class_rows = []
    for cls in sorted(dem.class_freq):
        if not is_android(cls):
            continue
        # APK 自带类（android.support.* / android.zhihu.* 等打进考卷 dex 的）
        # 不是 framework 需求，单独归类。
        if cls in dem.apk_classes:
            src = "apk"
        else:
            src = covered_src.get(cls, "MISS")
        class_rows.append(
            (
                cls,
                dem.class_freq[cls],
                dem.class_startup_freq.get(cls, 0),
                len(dem.class_dexes[cls]),
                len(dem.class_referrers[cls]),
                src,
            )
        )
    class_rows.sort(key=lambda r: (-max(r[2], 0) - r[1] * 0.001, -r[2], -r[1]))

    member_rows = []
    for cls in sorted(dem.member_freq):
        if not is_android(cls) or cls in dem.apk_classes:
            continue
        cov = covered_members.get(cls, set())
        for key in sorted(dem.member_freq[cls]):
            base = key.split("(")[0]
            if key.startswith("f:"):
                hit = key in cov or key[2:] in {c[2:] for c in cov if c.startswith("f:")}
            else:
                hit = any(c == base or c == key or c.startswith(base + "(") for c in cov)
            member_rows.append(
                (
                    cls,
                    key,
                    dem.member_freq[cls][key],
                    dem.member_startup_freq.get(cls, {}).get(key, 0),
                    covered_src.get(cls, "MISS"),
                    "Y" if hit else "-",
                    "|".join(sorted(dem.member_sig[cls].get(key, ())))[:400],
                )
            )
    member_rows.sort(key=lambda r: (-r[3], -r[2], r[0], r[1]))

    # ---- 报告 ----
    buf = io.StringIO()
    total_cls = len([r for r in class_rows if r[5] != "apk"])
    apk_cls = [r for r in class_rows if r[5] == "apk"]
    gap_cls = [r for r in class_rows if r[5] == "MISS"]
    cov_cls = [r for r in class_rows if r[5] in ("builtin", "lib")]
    total_mem = len(member_rows)
    gap_mem = [r for r in member_rows if r[5] == "-" ]
    cls_cov_rate = (100.0 * len(cov_cls) / total_cls) if total_cls else 0.0
    mem_cov_rate = (100.0 * (total_mem - len(gap_mem)) / total_mem) if total_mem else 0.0

    def family(cls):
        parts = cls.split(".")
        return ".".join(parts[:2]) if len(parts) >= 2 else cls

    fam = defaultdict(lambda: [0, 0, 0])  # fam -> [freq, startup_freq, classes]
    for cls, f, sf, _d, _r, cov in class_rows:
        if is_android(cls) and cov != "apk":
            fa = family(cls)
            fam[fa][0] += f
            fam[fa][1] += sf
            fam[fa][2] += 1

    w = buf.write
    w("== zhihu.apk android.* 需求面静态提取（framework 面批量补充轮） ==\n")
    w(f"target: {args.apk}\n")
    w(f"dex: {len(dex_names)} ({', '.join(dex_names[:4])}...)\n")
    w(f"apk classes: {len(dem.apk_classes)}   const-string 类名 hit: {sum(dem.const_string_hits.values())}\n")
    w(f"{manifest_note}\n")
    w(f"startup closure: {len(startup_set)} apk classes (cap {args.startup_cap})\n")
    w("\n-- 摘要 --\n")
    w(f"android.* 引用类: {total_cls} (framework 需求口径；APK 自带 {len(apk_cls)} 个已剔除)  已覆盖: {len(cov_cls)}  缺口: {len(gap_cls)}  覆盖率(类): {cls_cov_rate:.1f}%\n")
    w(f"android.* 引用成员: {total_mem}  缺口成员: {len(gap_mem)}  覆盖率(成员名): {mem_cov_rate:.1f}%\n")
    w("\n-- 家族排序（freq / startup_freq / classes）--\n")
    for fa, (f, sf, c) in sorted(fam.items(), key=lambda kv: (-kv[1][1], -kv[1][0])):
        w(f"  {fa:<28} {f:>8} {sf:>8} {c:>5}\n")

    w(f"\n-- 类级需求 top {args.top}（startup 频次优先）--\n")
    w(f"  {'class':<52} {'freq':>7} {'sFreq':>7} {'dex':>4} {'ref':>5}  cov\n")
    for cls, f, sf, d, r, cov in class_rows[: args.top]:
        w(f"  {cls:<52} {f:>7} {sf:>7} {d:>4} {r:>5}  {cov}\n")

    w(f"\n-- 类级缺口清单（{len(gap_cls)}）按 startup 频次排序 --\n")
    for cls, f, sf, d, r, _ in gap_cls:
        w(f"  {cls:<52} freq={f:>6} sFreq={sf:>6} dex={d} referrers={r}\n")

    w(f"\n-- 成员级需求 top {args.top}（startup 频次优先；cov=Y 表示已有）--\n")
    w(f"  {'class':<40} {'member':<34} {'freq':>6} {'sFreq':>6} cov\n")
    for cls, key, f, sf, _src, hit, _sig in member_rows[: args.top]:
        w(f"  {cls:<40} {key:<34} {f:>6} {sf:>6} {hit}\n")

    w(f"\n-- 成员级缺口 top {args.top}（已覆盖类内缺失成员；按 sFreq）--\n")
    shown = 0
    for cls, key, f, sf, src, hit, _sig in member_rows:
        if hit == "Y" or shown >= args.top:
            continue
        if src == "MISS":
            continue  # 整类缺失已在类级清单
        w(f"  {cls:<40} {key:<34} {f:>6} {sf:>6} [{src}]\n")
        shown += 1

    w("\n-- const-string 类名引用（反射/Class.forName 面）top 30 --\n")
    for s, c in sorted(dem.const_string_hits.items(), key=lambda kv: -kv[1])[:30]:
        w(f"  {s:<60} {c}\n")

    w("\n-- 缺口成员精确签名（dex proto 全形；startup 频次 top 40）--\n")
    shown = 0
    for cls, key, _f, _sf, src, hit, sig in member_rows:
        if shown >= 40:
            break
        if hit == "Y" or src == "MISS" or not sig:
            continue
        w(f"  {cls}.{sig}\n")
        shown += 1

    w("\n-- 覆盖率口径 --\n")
    w("类级: builtin_classes.def 注册集 ∪ runtime-libs java 源 ∪ C++ MakeClass 注册面。\n")
    w("成员级: runtime-libs 方法/字段声明（名+元数）∪ C++ AddIntrinsic*/Add*Field 名。\n")
    w("builtin 成员抽取为正则尽力（类级权威、成员级 best-effort）。\n")

    report_path = os.path.join(out_dir, "report.txt")
    with open(report_path, "w") as f:
        f.write(buf.getvalue())

    with open(os.path.join(out_dir, "demand_classes.tsv"), "w") as f:
        f.write("class\tfreq\tstartup_freq\tdex\treferrers\tcovered\n")
        for cls, fr, sf, d, r, cov in class_rows:
            f.write(f"{cls}\t{fr}\t{sf}\t{d}\t{r}\t{cov}\n")
    with open(os.path.join(out_dir, "demand_members.tsv"), "w") as f:
        f.write("class\tmember\tfreq\tstartup_freq\tclass_covered\tmember_covered\tsigs\n")
        for cls, key, fr, sf, src, hit, sig in member_rows:
            f.write(f"{cls}\t{key}\t{fr}\t{sf}\t{src}\t{hit}\t{sig}\n")

    print(f"[demand] report -> {report_path}")
    print(
        f"[demand] classes: total={total_cls} covered={len(cov_cls)} gap={len(gap_cls)} "
        f"({cls_cov_rate:.1f}%)  members: total={total_mem} gap={len(gap_mem)} ({mem_cov_rate:.1f}%)"
    )


if __name__ == "__main__":
    main()
