#!/usr/bin/env bash
# 泛型反射面 + java.lang.ref×ThreadLocal 压力用例（独立；不动既有 tools/*.sh 与 tests 既有文件）。
# 覆盖：
#   1) Class.getGenericSuperclass/getGenericInterfaces/getTypeParameters
#      （dex Signature 属性解析 → lib Type 镜像语义）
#   2) ClassCastException: SoftReference -> ThreadLocal 复现/回归
#      （ThreadLocalMap$Entry extends WeakReference 链布局混型）
# 用法: tools/generics_ref_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_generics_ref_tests"
FIXTURE_OUT="$CPP/tests/out"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "generics_ref_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "generics_ref_tests: d8 not found: $D8" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量）
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
  if [ -z "$GRADLE" ]; then
    echo "generics_ref_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
    exit 1
  fi
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "generics_ref_tests: runtime.dex missing: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/generics_ref-src -> tests/out/generics_ref.dex）
echo "==== build fixture (generics_ref.dex) ===="
SRC_DIR="$CPP/tests/generics_ref-src"
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
  echo "generics_ref_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

# 注意：java/lang/RefMapProbe 需要包内可见 ThreadLocal$ThreadLocalMap 面，
# ct.sym（--release）不含包私有成员——用 -source/-target 8 对完整 JDK 编译。
"$JAVAC" -source 8 -target 8 -Xlint:-options -encoding UTF-8 -g -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR" -name '*.class' | sort)
EOF
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "generics_ref_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/generics_ref.dex"
echo "wrote $FIXTURE_OUT/generics_ref.dex"

# 2. 编译 harness（core VM 全量 + session）
echo "==== compile generics_ref_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/generics_ref_smoke.cc" \
  -lz \
  -o "$OUT/generics_ref_smoke"

# 3. 执行（FAIL 行/退出码即结果）
echo "==== run generics_ref_smoke ===="
exec "$OUT/generics_ref_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/generics_ref.dex"
