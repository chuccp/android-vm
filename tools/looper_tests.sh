#!/usr/bin/env bash
# M4.3：android.os 消息循环子系统宿主测试（独立脚本，不动既有 tools/*.sh）。
# 纯内建引导（无需 runtime.dex/fixture APK）：Looper/Handler/MessageQueue/Message/
# HandlerThread/Process/SystemClock 语义面 + 消息泵（docs 04 章 14.10）。
# 用法: tools/looper_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_looper_tests"

# 编译（core 内建 + framework 内建 + session IO + dex + zlib；不含 lifecycle_driver）
echo "==== compile looper_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/looper_smoke.cc" \
  -lz \
  -o "$OUT/looper_smoke"

# 执行
echo "==== run looper_smoke ===="
exec "$OUT/looper_smoke"
