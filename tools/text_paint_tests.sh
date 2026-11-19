#!/usr/bin/env bash
# TextPaint/Paint/Typeface/StaticLayout 文本测量面宿主测试（framework 面批量补充轮）。
# boot classpath = runtime-libs/build/runtime-dex/runtime.dex（lib 版 android.text/
# android.graphics 面），断言字段态/测量缝（与 render.cc TextMeasure* 同网格口径）
# + StaticLayout 行折断 + 像素断言（Layout.draw -> FrameBuffer 墨迹范围 == 测量值）。
# 独立脚本（不动既有 tools/*.sh）。
# 用法: tools/text_paint_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_text_paint_tests"

RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"

# gradle 9.6.0（wrapper dist；local.properties/sdk.dir 已配）
GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "text_paint_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
  exit 1
fi

# 0. 产物构建（gradle 增量；缺什么补什么）
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "text_paint_tests: runtime.dex missing after build: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. 编译（core 内建 + framework 内建 + 渲染管线 + UI Bridge + session IO + zlib）
echo "==== compile text_paint_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP/framework/render.cc" \
  "$CPP/bridge/bridge.cc" \
  "$CPP/bridge/ui_bridge_protocol.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/text_paint_smoke.cc" \
  -lz \
  -o "$OUT/text_paint_smoke"

# 2. 执行
echo "==== run text_paint_smoke ===="
exec "$OUT/text_paint_smoke" "$RUNTIME_DEX"
