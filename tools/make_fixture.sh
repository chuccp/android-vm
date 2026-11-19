#!/usr/bin/env bash
# 从 app/src/main/cpp/tests/fixture-src 编译 fixture Java 源，
# 产出 app/src/main/cpp/tests/out/fixture.dex。
# javac（Android Studio JBR）编译 class，d8（build-tools）转 DEX。
#
# 用法: tools/make_fixture.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP_ROOT="$REPO_ROOT/app/src/main/cpp"
SRC_DIR="$CPP_ROOT/tests/fixture-src"
OUT_DIR="$CPP_ROOT/tests/out"
CLASSES_DIR="$OUT_DIR/classes"
D8_DIR="$OUT_DIR/d8-tmp"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "make_fixture: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "make_fixture: d8 not found: $D8" >&2
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
  echo "make_fixture: no java sources under $SRC_DIR" >&2
  exit 1
fi

echo "make_fixture: javac ${#JAVA_SOURCES[@]} sources"
"$JAVAC" --release 8 -encoding UTF-8 -g -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR" -name '*.class' | sort)
EOF
echo "make_fixture: d8 ${#CLASS_FILES[@]} classes"
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"

if [[ ! -f "$D8_DIR/classes.dex" ]]; then
  echo "make_fixture: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$OUT_DIR/fixture.dex"
rm -rf "$D8_DIR"
echo "make_fixture: wrote $OUT_DIR/fixture.dex ($(wc -c < "$OUT_DIR/fixture.dex" | tr -d ' ') bytes)"
