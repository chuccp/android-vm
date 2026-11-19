#!/usr/bin/env python3
"""内建/lib 分界审计（§31.7 口径自动盘点）。

输入：
  - app/src/main/cpp/core/builtin_classes.def（内建类清单）
  - runtime-libs/src/main/java（lib 源）
  - /tmp/vm_api.txt（tests/core/api_dump.cc：内建类方法面 + N/C 实现标记）

输出两类问题：

  [DUP] 同一个类既有内建注册、又有 lib 源（双实现——内建遮蔽 lib，lib 源成为
        死代码/漂移源；ComponentName 事故形态）。处置：要么删内建让 lib 生效
        （纯逻辑类默认），要么删 lib 源（必须内建时）。

  [LIB_CANDIDATE] 内建类里没有任何 N（intrinsic）方法——全是骨架/纯 API 面，
        按「默认 lib 优先」应迁移到 lib Java 源（C 档）。

  NONE_OF_METHODS / INTERFACE 的类跳过候选判定。
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEF = ROOT / "app/src/main/cpp/core/builtin_classes.def"
LIB_SRC = ROOT / "runtime-libs/src/main/java"
VM_API = Path("/tmp/vm_api.txt")

# VM 布局/mirror 类：即使当前无 intrinsic，也必须内建（实例字段由 VM 消费）。
MIRROR_KEEP = {
    "Ljava/lang/Object;", "Ljava/lang/String;", "Ljava/lang/Class;",
    "Ljava/lang/Thread;", "Ljava/lang/Throwable;", "Ljava/lang/reflect/Method;",
    "Ljava/lang/reflect/Field;", "Ljava/lang/reflect/Constructor;",
    "Ljava/lang/ClassLoader;", "Ljava/lang/StackTraceElement;",
}

def parse_def():
    pat = re.compile(r'^DEXVM_(?:CORE|FRAMEWORK)_BUILTIN_CLASS\("([^"]+)"\)')
    out = []
    for line in DEF.read_text().splitlines():
        m = pat.match(line.strip())
        if m:
            out.append(m.group(1))
    return out

def lib_source_for(desc: str):
    if not (desc.startswith("L") and desc.endswith(";")):
        return None
    path = desc[1:-1] + ".java"
    p = LIB_SRC / path
    return p if p.is_file() else None

def parse_api(path):
    """desc -> {'methods': n, 'intrinsics': n, 'iface': bool}"""
    info = {}
    cur = None
    for line in path.read_text().splitlines():
        if line.startswith("CLASS "):
            cur = line[6:].strip()
            info[cur] = {"methods": 0, "intrinsics": 0}
        elif cur and line.startswith("  "):
            parts = line.strip().split()
            if len(parts) < 2:
                continue
            info[cur]["methods"] += 1
            if parts[1] == "N":
                info[cur]["intrinsics"] += 1
    return info

def main():
    classes = parse_def()
    api = parse_api(VM_API)
    dups, candidates = [], []
    for desc in classes:
        src = lib_source_for(desc)
        if src is not None:
            dups.append((desc, src.relative_to(ROOT)))
        inf = api.get(desc)
        if inf and inf["methods"] > 0 and inf["intrinsics"] == 0 and desc not in MIRROR_KEEP:
            candidates.append((desc, inf["methods"]))
    print(f"builtin classes: {len(classes)}")
    print("\n== [DUP] builtin + lib source (双实现) ==")
    for desc, src in dups:
        print(f"  {desc}  <- {src}")
    if not dups:
        print("  (none)")
    print("\n== [LIB_CANDIDATE] builtin without any intrinsic (纯 API 面，建议挪 lib) ==")
    for desc, n in sorted(candidates, key=lambda x: -x[1]):
        print(f"  {desc}  methods={n}")
    if not candidates:
        print("  (none)")

if __name__ == "__main__":
    main()
