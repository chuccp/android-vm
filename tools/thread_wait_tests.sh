#!/usr/bin/env bash
# Thread.start/join/interrupt + Object.wait/notify/notifyAll + Unsafe.park/unpark
# 协程接线用例（独立；不动既有 tools/*.sh 与 tests 既有文件）。
# 覆盖：
#   1) Thread.start/join/interrupt 全语义（含 IllegalThreadStateException、
#      isAlive、join deadline、UEH 骨架）
#   2) 多 GuestTask 并发推进（动态线程数：8 线程各跑循环，互相观察推进）
#   3) Object.wait/notify/notifyAll（含超时/中断/IllegalMonitorState/重入恢复）
#   4) LinkedBlockingQueue.take/put 端到端（有界满 put 阻塞、FIFO）
#   5) Unsafe.park/unpark（permit/parkNanos 超时/安全 no-op）
# fixture 编译口径：--release 8（tests/thread_wait-src，类面 twtest/**）。
# 用法: tools/thread_wait_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_thread_wait_tests"
FIXTURE_OUT="$CPP/tests/out"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "thread_wait_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "thread_wait_tests: d8 not found: $D8" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量）
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
  if [ -z "$GRADLE" ]; then
    echo "thread_wait_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
    exit 1
  fi
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "thread_wait_tests: runtime.dex missing: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/thread_wait-src -> tests/out/thread_wait.dex）
echo "==== build fixture (thread_wait.dex) ===="
SRC_DIR="$CPP/tests/thread_wait-src"
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
  echo "thread_wait_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

"$JAVAC" --release 8 -Xlint:-options -encoding UTF-8 -g -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR/twtest" -name '*.class' | sort)
EOF
if [ ${#CLASS_FILES[@]} -eq 0 ]; then
  echo "thread_wait_tests: no probe classes under $CLASSES_DIR/twtest" >&2
  exit 1
fi
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "thread_wait_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/thread_wait.dex"
echo "wrote $FIXTURE_OUT/thread_wait.dex"

# 2. 编译 harness（core VM 全量 + coroutine + session）
echo "==== compile thread_wait_smoke ===="
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
  "$CPP/tests/core/thread_wait_smoke.cc" \
  -lz \
  -o "$OUT/thread_wait_smoke"

# 3. 执行（FAIL 行/退出码即结果）
echo "==== run thread_wait_smoke ===="
exec "$OUT/thread_wait_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/thread_wait.dex"
