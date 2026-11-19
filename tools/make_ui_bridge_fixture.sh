#!/usr/bin/env bash
# 从 app/src/main/cpp/tests/../../tests/ui-bridge-src 编译 UI 桥接 guest 探针
# Java 源，产出 app/src/main/cpp/tests/out/ui_bridge_fixture.dex。
# javac（Android Studio JBR）编译 class，d8（build-tools）转 DEX——
# 同 tools/make_fixture.sh 配方（不动既有工具）。
#
# 用法: tools/make_ui_bridge_fixture.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$REPO_ROOT/tests/ui-bridge-src"
OUT_DIR="$REPO_ROOT/app/src/main/cpp/tests/out"
CLASSES_DIR="$OUT_DIR/ui-bridge-classes"
D8_DIR="$OUT_DIR/ui-bridge-d8-tmp"
DEX_OUT="$OUT_DIR/ui_bridge_fixture.dex"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "make_ui_bridge_fixture: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "make_ui_bridge_fixture: d8 not found: $D8" >&2
  exit 1
fi

rm -rf "$CLASSES_DIR" "$D8_DIR"
mkdir -p "$CLASSES_DIR" "$D8_DIR"

JAVA_SOURCES=()
while IFS= read -r f; do
  JAVA_SOURCES[${#JAVA_SOURCES[@]}]="$f"
done <<EOF
$(find "$SRC_DIR" -name '*.java' | sort)
EOF
if [[ ${#JAVA_SOURCES[@]} -eq 0 ]]; then
  echo "make_ui_bridge_fixture: no java sources under $SRC_DIR" >&2
  exit 1
fi

echo "make_ui_bridge_fixture: javac ${#JAVA_SOURCES[@]} sources"
"$JAVAC" --release 8 -encoding UTF-8 -g -classpath "$ANDROID_JAR" -d "$CLASSES_DIR" \
  "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR" -name '*.class' | sort)
EOF

echo "make_ui_bridge_fixture: d8 -> $DEX_OUT"
"$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
cp "$D8_DIR/classes.dex" "$DEX_OUT"
echo "make_ui_bridge_fixture: wrote $DEX_OUT"
