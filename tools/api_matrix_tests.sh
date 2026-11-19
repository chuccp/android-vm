#!/usr/bin/env bash
# 解释器/API 流矩阵宿主测试（覆盖面参考启发自 v1 DexFixture/NativeVmCoreTest/
# NativePlatformMigrationTest/GuestHeapGcTest 覆盖面；测试本体按 v2 规格写）。
# 覆盖：静态链/对象流/数组/异常嵌套/clinit 计数/锁重入/Color.rgb/greeting +
# clinit 挂起/不重放四连 + WeakReference/ReferenceQueue/GC 面 + java.io 文件面。
# fixture 编译口径：--release 8（tests/api_matrix-src，类面 apitest/**）。
# 独立脚本（不动既有 tools/*.sh 与 tests 既有文件）。
# 用法: tools/api_matrix_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_api_matrix_tests"
FIXTURE_OUT="$CPP/tests/out"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "api_matrix_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "api_matrix_tests: d8 not found: $D8" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量）
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
  if [ -z "$GRADLE" ]; then
    echo "api_matrix_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
    exit 1
  fi
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "api_matrix_tests: runtime.dex missing: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/api_matrix-src -> tests/out/api_matrix.dex）
echo "==== build fixture (api_matrix.dex) ===="
SRC_DIR="$CPP/tests/api_matrix-src"
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
  echo "api_matrix_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

"$JAVAC" --release 8 -Xlint:-options -encoding UTF-8 -g -classpath "$ANDROID_JAR" \
  -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR/apitest" -name '*.class' | sort)
EOF
if [ ${#CLASS_FILES[@]} -eq 0 ]; then
  echo "api_matrix_tests: no probe classes under $CLASSES_DIR/apitest" >&2
  exit 1
fi
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "api_matrix_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/api_matrix.dex"
echo "wrote $FIXTURE_OUT/api_matrix.dex"

# 2. 编译 harness（core VM 全量 + coroutine + session）
echo "==== compile api_matrix_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -pthread \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/coroutine/guest_task.cc" \
  "$CPP/coroutine/scheduler.cc" \
  "$CPP/coroutine/fiber_switch.S" \
  "$CPP/tests/core/api_matrix_smoke.cc" \
  -lz \
  -o "$OUT/api_matrix_smoke"

# 3. 执行（文件面落 scratch cwd；FAIL 行/退出码即结果）
echo "==== run api_matrix_smoke ===="
WORK="$OUT/work"
rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"
exec "$OUT/api_matrix_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/api_matrix.dex"
