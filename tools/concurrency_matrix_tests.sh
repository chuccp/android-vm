#!/usr/bin/env bash
# 并发原语矩阵宿主测试（覆盖面参考启发自 v1 NativeVmModernConcurrencyTest.kt +
# RuntimeModernConcurrency.java；本体按 Android j.u.c/java.lang 语义写）。
# 覆盖：sleep/park/join/lock 中断、Condition、Semaphore（含公平序）、
# CountDownLatch、CyclicBarrier/Phaser、Future/CompletableFuture、
# ConcurrentHashMap、BlockingQueue 族、ThreadGroup 归属。
# fixture 编译口径：--release 11（CompletableFuture failedFuture/orTimeout/
# completeOnTimeout 为 Java 9+ API；tests/concurrency_matrix-src，类面 cmtest/**）。
# 独立脚本（不动既有 tools/*.sh 与 tests 既有文件）。
# 用法: tools/concurrency_matrix_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_concurrency_matrix_tests"
FIXTURE_OUT="$CPP/tests/out"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "concurrency_matrix_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "concurrency_matrix_tests: d8 not found: $D8" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量）
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
  if [ -z "$GRADLE" ]; then
    echo "concurrency_matrix_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
    exit 1
  fi
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "concurrency_matrix_tests: runtime.dex missing: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/concurrency_matrix-src -> tests/out/concurrency_matrix.dex）
echo "==== build fixture (concurrency_matrix.dex) ===="
SRC_DIR="$CPP/tests/concurrency_matrix-src"
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
  echo "concurrency_matrix_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

"$JAVAC" --release 11 -Xlint:-options -encoding UTF-8 -g -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR/cmtest" -name '*.class' | sort)
EOF
if [ ${#CLASS_FILES[@]} -eq 0 ]; then
  echo "concurrency_matrix_tests: no probe classes under $CLASSES_DIR/cmtest" >&2
  exit 1
fi
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "concurrency_matrix_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/concurrency_matrix.dex"
echo "wrote $FIXTURE_OUT/concurrency_matrix.dex"

# 2. 编译 harness（core VM 全量 + coroutine + session）
echo "==== compile concurrency_matrix_smoke ===="
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
  "$CPP/tests/core/concurrency_matrix_smoke.cc" \
  -lz \
  -o "$OUT/concurrency_matrix_smoke"

# 3. 执行（FAIL 行/退出码即结果）
echo "==== run concurrency_matrix_smoke ===="
exec "$OUT/concurrency_matrix_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/concurrency_matrix.dex"
