#!/usr/bin/env bash
# sun.misc.Unsafe legacy 门面用例（独立；不动既有 tools/*.sh 与 tests 既有文件）。
# 覆盖：
#   1) theUnsafe 获取（getDeclaredField("theUnsafe") -> setAccessible -> get(null)
#      反射链 + getUnsafe() + 单例同一性）
#   2) getInt/getObject/CAS 族（compareAndSwap{Object,Int,Long} + typed/volatile
#      镜像 + 数组/静态槽面 + 与解释器 iget/iput 窄化互见）
#   3) allocateInstance（v1 头号刚需：绕构造器分配；<clinit> 先行、abstract 抛
#      InstantiationException，ART jni AllocObject 同构）
# fixture 编译口径：--release 8 + stub（tests/unsafe_legacy-src/stub，仅 javac
# 编译面，不入 dex——运行期由 runtime.dex 的 sun.misc.Unsafe lib 壳承接）。
# 用法: tools/unsafe_legacy_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_unsafe_legacy_tests"
FIXTURE_OUT="$CPP/tests/out"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "unsafe_legacy_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "unsafe_legacy_tests: d8 not found: $D8" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量）
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
  if [ -z "$GRADLE" ]; then
    echo "unsafe_legacy_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
    exit 1
  fi
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "unsafe_legacy_tests: runtime.dex missing: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/unsafe_legacy-src -> tests/out/unsafe_legacy.dex）
echo "==== build fixture (unsafe_legacy.dex) ===="
SRC_DIR="$CPP/tests/unsafe_legacy-src"
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
  echo "unsafe_legacy_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

# stub（sun.misc.Unsafe）随源编译仅作 javac 编译面；d8 只收 untest/**，
# stub 类不入 fixture dex（运行期由 runtime.dex lib 壳承接）。
"$JAVAC" --release 8 -Xlint:-options -encoding UTF-8 -g -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR/untest" -name '*.class' | sort)
EOF
if [ ${#CLASS_FILES[@]} -eq 0 ]; then
  echo "unsafe_legacy_tests: no probe classes under $CLASSES_DIR/untest" >&2
  exit 1
fi
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --min-api 26 --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "unsafe_legacy_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/unsafe_legacy.dex"
echo "wrote $FIXTURE_OUT/unsafe_legacy.dex"

# 2. 编译 harness（core VM 全量 + session）
echo "==== compile unsafe_legacy_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/unsafe_legacy_smoke.cc" \
  -lz \
  -o "$OUT/unsafe_legacy_smoke"

# 3. 执行（FAIL 行/退出码即结果）
echo "==== run unsafe_legacy_smoke ===="
exec "$OUT/unsafe_legacy_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/unsafe_legacy.dex"
