#!/usr/bin/env bash
# java.security 链（Debug/SecureRandom/SHA1PRNG/SeedGenerator）+ Unsafe 对象
# 操作族 + java.io 文件面用例（独立；不动既有 tools/*.sh 与 tests 既有文件）。
# 覆盖：
#   1) sun.security.util.Debug：isOn/getInstance/pdebug 面/System.err 输出语义
#   2) SecureRandom -> Providers/SUN -> SHA1PRNG -> SeedGenerator /dev/urandom
#      （含 SHA1PRNG setSeed 确定性契约、UUID.randomUUID 链）
#   3) jdk.internal.misc.Unsafe 对象操作族（putObjectVolatile/getObjectVolatile/
#      compareAndSetObject/getAndSetObject/putOrderedObject + Int/Long 镜像）
#   4) java.io.FileInputStream/FileDescriptor native 缝（/dev/urandom 直读）
# fixture 编译口径：--release 8 + stub（tests/security_unsafe-src/stub，仅
# javac 编译面，不入 dex——运行期由 VM 内建 Unsafe / runtime.dex Debug 承接）。
# 用法: tools/security_unsafe_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_security_unsafe_tests"
FIXTURE_OUT="$CPP/tests/out"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "security_unsafe_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "security_unsafe_tests: d8 not found: $D8" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量）
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
  if [ -z "$GRADLE" ]; then
    echo "security_unsafe_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
    exit 1
  fi
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "security_unsafe_tests: runtime.dex missing: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/security_unsafe-src -> tests/out/security_unsafe.dex）
echo "==== build fixture (security_unsafe.dex) ===="
SRC_DIR="$CPP/tests/security_unsafe-src"
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
  echo "security_unsafe_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

# stub（jdk.internal.misc.Unsafe / sun.security.util.Debug）随源编译仅作
# javac 编译面；d8 只收 sectest/**，stub 类不入 fixture dex。
"$JAVAC" --release 8 -Xlint:-options -encoding UTF-8 -g -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR/sectest" -name '*.class' | sort)
EOF
if [ ${#CLASS_FILES[@]} -eq 0 ]; then
  echo "security_unsafe_tests: no probe classes under $CLASSES_DIR/sectest" >&2
  exit 1
fi
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "security_unsafe_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/security_unsafe.dex"
echo "wrote $FIXTURE_OUT/security_unsafe.dex"

# 2. 编译 harness（core VM 全量 + session）
echo "==== compile security_unsafe_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/security_unsafe_smoke.cc" \
  -lz \
  -o "$OUT/security_unsafe_smoke"

# 3. 执行（FAIL 行/退出码即结果）
echo "==== run security_unsafe_smoke ===="
exec "$OUT/security_unsafe_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/security_unsafe.dex"
