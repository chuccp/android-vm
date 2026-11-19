#!/usr/bin/env bash
# M4.2（收官迁移轮）：guest 渲染管线 / 首帧链宿主测试——桥接断言形。
# boot classpath = runtime-libs/build/runtime-dex/runtime.dex，
# app = target-app-debug.apk；断言 TextView/LinearLayout measure/layout 语义 +
# 缝A draw 命令流结构、target-app 首帧桥轨（ViewSnapshot 字段/revision/首帧
# 标记）与 "首帧完成" 状态（docs 10 章 19.4/19.6）。
# 独立脚本（不动既有 tools/*.sh）。
# 用法: tools/render_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_render_tests"

RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
TARGET_APK="$ROOT/target-app/build/outputs/apk/debug/target-app-debug.apk"

# gradle 9.6.0（wrapper dist；local.properties/sdk.dir 已配）
GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "render_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
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
  echo "render_tests: runtime.dex missing after build: $RUNTIME_DEX" >&2
  exit 1
fi
if [ ! -f "$TARGET_APK" ]; then
  echo "render_tests: target APK missing after build: $TARGET_APK" >&2
  exit 1
fi

# 1. 编译（core 内建 + framework 内建 + 渲染管线 + UI Bridge/协议 TU
#    + 生命周期驱动器 + session IO + zlib）
echo "==== compile render_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP/framework/lifecycle_driver.cc" \
  "$CPP/framework/render.cc" \
  "$CPP/bridge/bridge.cc" \
  "$CPP/bridge/ui_bridge_protocol.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/render_smoke.cc" \
  -lz \
  -o "$OUT/render_smoke"

# 2. 执行
echo "==== run render_smoke ===="
exec "$OUT/render_smoke" "$RUNTIME_DEX" "$TARGET_APK"
