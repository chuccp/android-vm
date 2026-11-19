#!/usr/bin/env bash
# java.lang.invoke 家族语义冒烟（JLI 修复轮验收线；独立脚本，不动既有 tools/tests）。
# 产物链：
#   runtime.dex（gradle :runtime-libs:build；家族核心类入包）
#   mh-fixture.dex（tests/mh-fixture-src → javac（--patch-module 对齐裁剪家族）
#                   → d8 --min-api 26：invoke-polymorphic 真 opcode 形）
# 断言：家族入包 / StackOverflowError.super / CLQ.<clinit> 全链 / guard_* 形状
#       与语义 / MethodHandle marker 面。
# 用法: tools/methodhandles_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_methodhandles_tests"
FIXTURE_OUT="$CPP/tests/out"

RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
RUNTIME_CLASSES="$ROOT/runtime-libs/build/runtime-classes"
FIXTURE_SRC="$CPP/tests/mh-fixture-src"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-37.0/android.jar"
[ -f "$ANDROID_JAR" ] || ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

# gradle 9.6.0（wrapper dist）
GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "methodhandles_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
  exit 1
fi

# 0. 产物（gradle 增量）
if [ ! -f "$RUNTIME_DEX" ] || [ ! -d "$RUNTIME_CLASSES" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "methodhandles_tests: runtime.dex missing after build: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. mh fixture：javac（--patch-module java.base=runtime-classes：对齐裁剪家族
#    签名，VarHandleGuards/AccessDescriptor.of/markers 可见）→ d8 min-api 26
echo "==== compile mh fixture ===="
mkdir -p "$OUT" "$FIXTURE_OUT"
rm -rf "$OUT/classes" "$OUT/d8-tmp"
mkdir -p "$OUT/classes" "$OUT/d8-tmp"
"$JAVAC" -source 11 -target 11 -nowarn -encoding UTF-8 -g \
  --patch-module "java.base=$RUNTIME_CLASSES" \
  --add-reads java.base=ALL-UNNAMED \
  -classpath "$ANDROID_JAR" \
  -d "$OUT/classes" \
  "$FIXTURE_SRC"/test/mh/MhChecks.java
echo "==== d8 mh fixture (min-api 26) ===="
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --min-api 26 --lib "$ANDROID_JAR" --output "$OUT/d8-tmp" \
  $(find "$OUT/classes" -name '*.class' | sort)
if [ ! -f "$OUT/d8-tmp/classes.dex" ]; then
  echo "methodhandles_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
cp "$OUT/d8-tmp/classes.dex" "$FIXTURE_OUT/mh-fixture.dex"
echo "wrote $FIXTURE_OUT/mh-fixture.dex"

# 2. 编译 methodhandles_smoke（core 源集，同 core_tests.sh 口径）
echo "==== compile methodhandles_smoke ===="
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/dex/dex_file.cc" \
  "$CPP/dex/dex_verify.cc" \
  "$CPP/dex/leb128.cc" \
  "$CPP/dex/mutf8.cc" \
  "$CPP/dex/opcodes.cc" \
  "$CPP/tests/core/methodhandles_smoke.cc" \
  -o "$OUT/methodhandles_smoke"

# 3. 跑（runtime.dex + mh-fixture.dex）
exec "$OUT/methodhandles_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/mh-fixture.dex"
