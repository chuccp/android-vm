#!/usr/bin/env bash
# SharedPreferences 面宿主测试（§31 A-框架层过渡内建）：
# boot classpath = runtime-libs/build/runtime-dex/runtime.dex（java.util
# HashMap/HashSet/装箱类来源），直接驱动 framework_builtins 的
# SharedPreferences/Editor/Listener intrinsic 面——单例语义/get-put 全型/
# remove/clear/commit 返回/apply 生效+后置回调/listener 注册。
# 独立脚本（不动既有 tools/*.sh）。
# 用法: tools/shared_prefs_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_shared_prefs_tests"

RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"

# gradle 9.6.0（wrapper dist；local.properties/sdk.dir 已配）
GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "shared_prefs_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
  exit 1
fi

# 0. 产物构建（gradle 增量；缺什么补什么）
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "shared_prefs_tests: runtime.dex missing after build: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. 编译（core 内建 + framework 内建 + session IO + zlib）
echo "==== compile shared_prefs_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/shared_prefs_smoke.cc" \
  -lz \
  -o "$OUT/shared_prefs_smoke"

# 2. 执行
echo "==== run shared_prefs_smoke ===="
exec "$OUT/shared_prefs_smoke" "$RUNTIME_DEX"
