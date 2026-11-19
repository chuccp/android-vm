#!/usr/bin/env python3
"""内建 API 面批量补桩生成器。

输入：
  - android.jar（本地 SDK）：javap -s -public 抽取公开方法 + DEX 描述符
  - /tmp/vm_api.txt：VM 内建已注册方法面（tests/core/api_dump.cc 输出）
  - app/src/main/cpp/core/builtin_classes.def：内建类清单

输出：
  - app/src/main/cpp/core/api_stubs.inc：DEXVM_STUB(...) 行（缺失方法 =
    可调用面桩；返回值按类型给默认语义，V=NoOp / 数值=0 / 引用=null）。

口径：
  - 只补 android.* / androidx 不涉及（非内建）/ dalvik.* / libcore.* /
    org.xmlpull.* 等 framework 面内建类；java.* 核心类不自动补（Shadow 风险，
    缺什么按 lib 修）。
  - 跳过构造器/静态初始化/抽象方法。
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEF = ROOT / "app/src/main/cpp/core/builtin_classes.def"
OUT = ROOT / "app/src/main/cpp/core/api_stubs.inc"
VM_API = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/vm_api.txt")

def find_android_jar() -> Path:
    sdk = Path.home() / "Library/Android/sdk/platforms"
    jars = sorted(sdk.glob("*/android.jar"), key=lambda p: p.parent.name)
    if not jars:
        sys.exit("android.jar not found under ~/Library/Android/sdk/platforms")
    return jars[-1]

AUTO_PREFIXES = ("Landroid/", "Ldalvik/", "Llibcore/", "Lorg/xmlpull/",
                 "Lorg/apache/http/", "Ljavax/")
SKIP_METHOD = {"<init>", "<clinit>"}

def parse_def() -> list[str]:
    out = []
    pat = re.compile(r'^DEXVM_(?:CORE|FRAMEWORK)_BUILTIN_CLASS\("([^"]+)"\)')
    for line in DEF.read_text().splitlines():
        m = pat.match(line.strip())
        if m:
            out.append(m.group(1))
    return out

def parse_vm_api(path: Path) -> dict[str, set[tuple[str, str]]]:
    """descriptor -> {(name, 'params)ret')}"""
    api: dict[str, set[tuple[str, str]]] = {}
    cur = None
    for line in path.read_text().splitlines():
        if line.startswith("CLASS "):
            cur = line[len("CLASS "):].strip()
            api.setdefault(cur, set())
        elif cur and line.startswith("  "):
            parts = line.strip().split()
            # 行格式："S|I  N|C  name(params)ret"
            if len(parts) < 3:
                continue
            sig = parts[2]
            m = re.match(r"([A-Za-z_$][\w$]*)\((.*)\)(.*)$", sig)
            if m:
                api[cur].add((m.group(1), m.group(2) + ")" + m.group(3)))
    return api

def javap_methods(jar: Path, cls: str) -> list[tuple[str, str, bool]]:
    """[(name, descriptor, is_static)]"""
    dot = cls[1:-1].replace("/", ".") if cls.startswith("L") else cls
    try:
        out = subprocess.run(
            ["javap", "-s", "-public", "-classpath", str(jar), dot],
            capture_output=True, text=True, timeout=60,
        ).stdout
    except subprocess.TimeoutExpired:
        return []
    methods = []
    pending_static = False
    pending_ok = False
    for line in out.splitlines():
        s = line.strip()
        if "(" in s and s.endswith(");"):
            pending_static = s.startswith("public static") or s.startswith("protected static")
            pending_ok = not s.startswith("private")
            m = re.search(r"([A-Za-z_$][\w$]*)\s*\(", s)
            pending_name = m.group(1) if m else None
        elif s.startswith("descriptor:") and pending_ok and pending_name:
            desc = s.split("descriptor:", 1)[1].strip()
            mm = re.match(r"\((.*)\)(.*)$", desc)
            simple = dot.rsplit(".", 1)[-1].split("$")[-1]
            is_ctor = pending_name in SKIP_METHOD or pending_name == simple or (
                pending_name.replace("$", ".") == dot.rsplit(".", 1)[-1])
            if mm and not is_ctor:
                methods.append((pending_name, mm.group(1) + ")" + mm.group(2), pending_static))
            pending_ok = False
    return methods

def params_literal(params: str) -> str:
    out, i = [], 0
    while i < len(params):
        c = params[i]
        if c == "[":
            j = i
            while j < len(params) and params[j] == "[":
                j += 1
            if params[j] == "L":
                k = params.index(";", j)
                out.append(params[i:k + 1])
                i = k + 1
            else:
                out.append(params[i:j + 1])
                i = j + 1
        elif c == "L":
            k = params.index(";", i)
            out.append(params[i:k + 1])
            i = k + 1
        else:
            out.append(c)
            i += 1
    return '"' + "|".join(out) + '"' 

def main() -> None:
    jar = find_android_jar()
    classes = [c for c in parse_def() if c.startswith(AUTO_PREFIXES)]
    vm = parse_vm_api(VM_API)
    total, stub = 0, 0
    lines = ["// 生成文件：tools/api_stub_gen.py（android.jar API 面差集）——勿手改。",
             ""]
    for cls in classes:
        have = vm.get(cls, set())
        for name, sig, is_static in javap_methods(jar, cls):
            total += 1
            if (name, sig) in have:
                continue
            stub += 1
            ret = sig.rsplit(")", 1)[1]
            lines.append(
                'DEXVM_STUB("%s", "%s", %s, "%s", %s)' %
                (cls, name, params_literal(sig.rsplit(")", 1)[0]),
                 ret, "true" if is_static else "false"))
            have.add((name, sig))
    lines.append("")
    # 去重（同签名多次出现时后者覆盖前者）
    seen = set()
    dedup = []
    for ln in lines:
        if ln.startswith("DEXVM_STUB("):
            if ln in seen:
                continue
            seen.add(ln)
        dedup.append(ln)
    OUT.write_text("\n".join(dedup))
    print(f"classes={len(classes)} sdk_methods={total} stubs={len(seen)} -> {OUT}")

if __name__ == "__main__":
    main()
