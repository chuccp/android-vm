#!/usr/bin/env bash
# Handler.obtainMessage 全家族 + Message 面（obtain 全重载/recycle 复用语义）宿主测试
# （独立脚本，不动既有 tools/*.sh；对照 looper_tests.sh 同款纯内建引导，无需 runtime.dex）。
# 用法: tools/handler_msg_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_handler_msg_tests"

# 编译（core 内建 + framework 内建 + session IO + dex + zlib；不含 lifecycle_driver）
echo "==== compile handler_msg_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/handler_msg_smoke.cc" \
  -lz \
  -o "$OUT/handler_msg_smoke"

# 执行
echo "==== run handler_msg_smoke ===="
exec "$OUT/handler_msg_smoke"
