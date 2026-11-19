#!/usr/bin/env bash
# java.net socket native 面（Q2 缝）宿主语义测试——独立脚本（不动 tools/host_tests.sh
# 与既有 tools/tests）。fixture 线（tests/net-src -> tests/out/net.dex）跑在
# net_smoke（core VM + runtime.dex boot）：
#   1) DNS 解析（getByName/getAllByName：localhost/字面量/未知主机）
#   2) socket 回环 echo（create/connect/bind/listen/accept/read/write/close）
#   3) socket 选项面（TCP_NODELAY/SO_TIMEOUT/SO_REUSEADDR/SO_KEEPALIVE）
#   4) available / shutdownOutput EOF 语义
#   5) fetch 收敛语义（失败轮 ConnectException + HTTP/1.0 mock server 成功轮）
# 常规 + ASAN 双过。用法: tools/net_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_net_tests"
FIXTURE_OUT="$CPP/tests/out"
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "net_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "net_tests: d8 not found: $D8" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量——runtime-libs 补源变更会重打包）
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
  if [ -z "$GRADLE" ]; then
    echo "net_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
    exit 1
  fi
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "net_tests: runtime.dex missing after build: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/net-src -> tests/out/net.dex）
echo "==== build fixture (net.dex) ===="
SRC_DIR="$CPP/tests/net-src"
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
  echo "net_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

"$JAVAC" --release 8 -Xlint:-options -encoding UTF-8 -g -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR" -name '*.class' | sort)
EOF
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "net_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/net.dex"
echo "wrote $FIXTURE_OUT/net.dex"

# 2. 编译 harness（core VM + session）
echo "==== compile net_smoke ===="
mkdir -p "$OUT"
SOURCES=(
  "$CPP"/core/*.cc
  "$CPP/memory/heap.cc"
  "$CPP/session/session.cc"
  "$CPP/session/class_loader.cc"
  "$CPP"/dex/*.cc
  "$CPP/tests/core/net_smoke.cc"
)

xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/net_smoke"

echo "==== run net_smoke (regular) ===="
"$OUT/net_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/net.dex"

# 3. ASAN 双过
echo "==== compile net_smoke (ASAN) ===="
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -fsanitize=address \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/net_smoke_asan"

echo "==== run net_smoke (ASAN) ===="
"$OUT/net_smoke_asan" "$RUNTIME_DEX" "$FIXTURE_OUT/net.dex"

echo "==== net tests: PASS ===="
