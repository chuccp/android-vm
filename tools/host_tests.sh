#!/usr/bin/env bash
# 宿主测试统一入口：DEX 解析测试 + 核心 VM（解释器）测试
# 用法: tools/host_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_host_tests"

# 0. fixture（dex 测试需要）
if [ ! -f "$CPP/tests/out/fixture.dex" ]; then
  "$ROOT/tools/make_fixture.sh"
fi

# 1. DEX 解析测试
echo "==== dex tests ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/dex/dex_tests.cc" \
  -o "$OUT/dex_tests"
"$OUT/dex_tests" "$CPP/tests/out/fixture.dex"

# 2. 核心 VM 测试（解释器冒烟等）
echo "==== core tests ===="
"$ROOT/tools/core_tests.sh"

# 3. GC 冒烟
echo "==== gc tests ===="
"$ROOT/tools/gc_tests.sh"

# 4. JNI 冒烟
echo "==== jni tests ===="
"$ROOT/tools/jni_tests.sh"

# 5. 协程冒烟
echo "==== coroutine tests ===="
"$ROOT/tools/coroutine_tests.sh"

# 6. ClassLoader/DexFile（M2）
echo "==== classloader tests ===="
"$ROOT/tools/classloader_tests.sh"

# 7. JNI 全面（M2）
echo "==== jni full tests ===="
"$ROOT/tools/jni_full_tests.sh"

# 8. 真 APK 全链执行（M3 阶段 1）
echo "==== apk run tests ===="
"$ROOT/tools/apk_run_tests.sh"

# 9. Framework 生命周期链（M4 阶段 1）
echo "==== lifecycle tests ===="
"$ROOT/tools/lifecycle_tests.sh"

# 10. UI 渲染/首帧链（M4 阶段 2）
echo "==== render tests ===="
exec "$ROOT/tools/render_tests.sh"
