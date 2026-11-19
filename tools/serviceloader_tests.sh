#!/usr/bin/env bash
# java.util.ServiceLoader（M5）宿主测试：编译并跑 tests/core/serviceloader_smoke.cc。
# 独立脚本（不动既有 tools/*.sh）。
# 用法: tools/serviceloader_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_serviceloader_tests"

mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/serviceloader_smoke.cc" \
  -lz \
  -o "$OUT/serviceloader_smoke"

exec "$OUT/serviceloader_smoke" "$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
