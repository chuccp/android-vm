#!/usr/bin/env bash
# 核心 VM 宿主测试：编译并跑 tests/core/*（解释器冒烟等）。
# 用法: tools/core_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_core_tests"

xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/dex/dex_file.cc" \
  "$CPP/dex/dex_verify.cc" \
  "$CPP/dex/leb128.cc" \
  "$CPP/dex/mutf8.cc" \
  "$CPP/dex/opcodes.cc" \
  "$ROOT/app/src/main/cpp/tests/core/interpreter_smoke.cc" \
  -o "$OUT"

exec "$OUT"
