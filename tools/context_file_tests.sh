#!/usr/bin/env bash
# Context 文件/存储面 + Proxy 面宿主测试（独立脚本，不动既有 tools/*.sh）。
# fixture 线（tests/context-src -> tests/out/context.dex）跑在 context_file_smoke
# （core VM + framework 内建 + runtime.dex boot）：
#   1) getDataDir/getFilesDir/getCacheDir/getCodeCacheDir/getNoBackupFilesDir
#      语义（路径自洽 = filesDir.parent == dataDir、目录取用即建）
#   2) File 面读写（FileOutputStream/FileInputStream 字节/长度自洽）
#   3) getDir/getDatabasePath/getDatabaseNames/deleteDatabase/
#      getSharedPreferencesPath/move*From 族语义
#   4) Proxy 面（分发/装箱拆箱/元面/handler 抛出/Method.invoke 委托链）
# 常规 + ASAN 双过。用法: tools/context_file_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_context_file_tests"
FIXTURE_OUT="$CPP/tests/out"
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "context_file_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "context_file_tests: d8 not found: $D8" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量——runtime-libs 变更会重打包）
GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "context_file_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
  exit 1
fi
if [ ! -f "$RUNTIME_DEX" ] || [ -n "$(find "$ROOT/runtime-libs/src/main/java" -name '*.java' -newer "$RUNTIME_DEX" 2>/dev/null | head -1)" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "context_file_tests: runtime.dex missing after build: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/context-src -> tests/out/context.dex）
echo "==== build fixture (context.dex) ===="
SRC_DIR="$CPP/tests/context-src"
CLASSES_DIR="$OUT/classes"
D8_DIR="$OUT/d8-tmp"
rm -rf "$CLASSES_DIR" "$D8_DIR"
mkdir -p "$CLASSES_DIR" "$D8_DIR" "$FIXTURE_OUT"

JAVA_SOURCES=()
while IFS= read -r f; do
  JAVA_SOURCES[${#JAVA_SOURCES[@]}]="$f"
done <<EOF
$(find "$SRC_DIR" -name '*.java' | sort)
EOF
if [ ${#JAVA_SOURCES[@]} -eq 0 ]; then
  echo "context_file_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

"$JAVAC" --release 8 -Xlint:-options -encoding UTF-8 -g -classpath "$ANDROID_JAR" -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR" -name '*.class' | sort)
EOF
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "context_file_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/context.dex"
echo "wrote $FIXTURE_OUT/context.dex"

# 2. 编译 harness（core VM + framework 内建 + session IO）
SOURCES=(
  "$CPP"/core/*.cc
  "$CPP/memory/heap.cc"
  "$CPP/session/session.cc"
  "$CPP/session/class_loader.cc"
  "$CPP"/dex/*.cc
  "$CPP/tests/core/context_file_smoke.cc"
)

echo "==== compile context_file_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/context_file_smoke"

# 3. 执行（数据根 = $OUT/data，每轮全新）
DATA_DIR="$OUT/data"
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR"

echo "==== run context_file_smoke (regular) ===="
"$OUT/context_file_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/context.dex" "$DATA_DIR"

# 4. ASAN 双过
echo "==== compile context_file_smoke (ASAN) ===="
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -fsanitize=address \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/context_file_smoke_asan"

rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR"
echo "==== run context_file_smoke (ASAN) ===="
"$OUT/context_file_smoke_asan" "$RUNTIME_DEX" "$FIXTURE_OUT/context.dex" "$DATA_DIR"

echo "==== context_file tests: PASS ===="
