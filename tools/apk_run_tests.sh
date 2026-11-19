#!/usr/bin/env bash
# M3 阶段 1（收官迁移轮）：target-app 考卷 APK 在 VM 里执行的宿主测试。
# boot classpath = runtime-libs/build/runtime-dex/runtime.dex（gradle 流水线产物），
# app = target-app/build/outputs/apk/debug/target-app-debug.apk（缺则
# :target-app:assembleDebug 现构）。
# 断言 = 34 类分发契约的哨兵 int 表（VM 语义面一组，见 apk_run_smoke.cc）。
# 用法: tools/apk_run_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_apk_run_tests"

RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
TARGET_APK="$ROOT/target-app/build/outputs/apk/debug/target-app-debug.apk"

# gradle 9.6.0（wrapper dist；local.properties/sdk.dir 已配）
GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "apk_run_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
  exit 1
fi

# 0. 产物构建（gradle 增量；缺什么补什么）
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$TARGET_APK" ]; then
  echo "==== build target-app (target-app-debug.apk) ===="
  "$GRADLE" -p "$ROOT" :target-app:assembleDebug
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "apk_run_tests: runtime.dex missing after build: $RUNTIME_DEX" >&2
  exit 1
fi
if [ ! -f "$TARGET_APK" ]; then
  echo "apk_run_tests: target APK missing after build: $TARGET_APK" >&2
  exit 1
fi

# 1. 编译（DEXVM_SESSION_IO：session/*.cc + zlib，APK zip 解析；
#    coroutine：RunGuestMain main GuestTask 上下文——挂起式 clinit 探针需要）
echo "==== compile apk_run_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -pthread \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP/coroutine/guest_task.cc" \
  "$CPP/coroutine/scheduler.cc" \
  "$CPP/coroutine/fiber_switch.S" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/apk_run_smoke.cc" \
  -lz \
  -o "$OUT/apk_run_smoke"

# 2. 执行
echo "==== run apk_run_smoke ===="
exec "$OUT/apk_run_smoke" "$RUNTIME_DEX" "$TARGET_APK"
