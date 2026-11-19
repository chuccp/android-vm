#!/usr/bin/env bash
# dirty reference 卫生墙压力（死对象槽形态复现/回归；独立脚本，不动既有
# tools/tests）：juc/ThreadPoolExecutor/ReentrantLock 链 + 继承链引用槽 +
# GC 穿插 + iget 消费。覆盖形态：
#   inherit2/inherit3  2/3 级继承链引用槽（GC 漏标超类字段 = 死对象槽根因）
#   pool               ThreadPoolExecutor 子类（设备停点：ensureInitialized
#                      iget-object workQueue @ pc=0 同字节码面）
#   lock               ReentrantLock 子类（sync/AQS 继承槽）
#   plain              纯 ThreadPoolExecutor 对照（自家槽口径恒绿）
#   refkeep            WeakReference.referent 强引用口径（docs 语义）
# 常规 + ASAN 双过。fixture 编译口径：--release 8（tests/hygiene-src）。
# 用法: tools/hygiene_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_hygiene_tests"
FIXTURE_OUT="$CPP/tests/out"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "hygiene_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "hygiene_tests: d8 not found: $D8" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量）
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
  if [ -z "$GRADLE" ]; then
    echo "hygiene_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
    exit 1
  fi
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "hygiene_tests: runtime.dex missing: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/hygiene-src -> tests/out/hygiene.dex）
echo "==== build fixture (hygiene.dex) ===="
SRC_DIR="$CPP/tests/hygiene-src"
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
  echo "hygiene_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

"$JAVAC" --release 8 -Xlint:-options -encoding UTF-8 -g -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR/htest" -name '*.class' | sort)
EOF
if [ ${#CLASS_FILES[@]} -eq 0 ]; then
  echo "hygiene_tests: no probe classes under $CLASSES_DIR/htest" >&2
  exit 1
fi
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "hygiene_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/hygiene.dex"
echo "wrote $FIXTURE_OUT/hygiene.dex"

# 2. 编译 harness（core VM 全量 + session）
echo "==== compile hygiene_smoke ===="
mkdir -p "$OUT"
SOURCES=(
  "$CPP"/core/*.cc
  "$CPP/memory/heap.cc"
  "$CPP/session/session.cc"
  "$CPP/session/class_loader.cc"
  "$CPP"/dex/dex_file.cc
  "$CPP"/dex/dex_verify.cc
  "$CPP"/dex/leb128.cc
  "$CPP"/dex/mutf8.cc
  "$CPP"/dex/opcodes.cc
  "$CPP/tests/core/hygiene_smoke.cc"
)

xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -pthread \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/hygiene_smoke"

# 3. 执行（FAIL 行/退出码即结果）
echo "==== run hygiene_smoke (regular) ===="
"$OUT/hygiene_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/hygiene.dex"

# 4. ASAN 双过（精确根口径下同断言）
echo "==== compile hygiene_smoke (ASAN) ===="
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -pthread \
  -fsanitize=address \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/hygiene_smoke_asan"

echo "==== run hygiene_smoke (ASAN) ===="
ASAN_OPTIONS=detect_stack_use_after_return=0:abort_on_error=0 \
  "$OUT/hygiene_smoke_asan" "$RUNTIME_DEX" "$FIXTURE_OUT/hygiene.dex"

echo "==== hygiene tests: ALL PASS ===="
