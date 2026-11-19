#!/usr/bin/env bash
# 协程运行时宿主测试：编译并跑 tests/core/coroutine_smoke.cc（docs 04 章 14 章 M1 面）。
# 独立脚本：不改 tools/host_tests.sh / tools/core_tests.sh（集成由集成方合并）。
# 用法: tools/coroutine_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_coroutine_tests"

mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -Wno-deprecated-declarations \
  -pthread \
  -I "$CPP" \
  "$CPP/coroutine/guest_task.cc" \
  "$CPP/coroutine/fiber_switch.S" \
  "$CPP/coroutine/scheduler.cc" \
  "$ROOT/app/src/main/cpp/tests/core/coroutine_smoke.cc" \
  -o "$OUT/coroutine_tests"

exec "$OUT/coroutine_tests"
