#!/usr/bin/env bash
# Resources 面 + grantUriPermissions 投影宿主测试（独立脚本，不动既有 tools/*.sh）。
# fixture 线（tests/resource-src -> tests/out/resource.apk：javac+d8 出 classes.dex +
# aapt2 compile res/xml + link 真 binary AndroidManifest.xml/AXML）跑在 resource_smoke
# （core VM + framework 内建 + runtime.dex boot + 生命周期驱动器）：
#   1) grantUri 投影断言：ProviderEntry.grant_uri_permissions（0x0101001b 真值 +
#      缺省 false 两面）+ BuildProviderInfo 投影（fixture attachInfo 同
#      FileProvider SecurityException 消费面）
#   2) resources.arsc/AXML 缝：LookupResourceId/ReadResourceFile/ParseResourceXml
#      （res/xml/file_paths.xml：paths 四 tag name/path 属性）
#   3) guest API 面（fixture 探针 = androidx FileProvider 消费链同款字节序）：
#      resolveContentProvider -> loadXmlMetaData -> getResourcesForApplication ->
#      Resources.getXml -> XmlResourceParser 事件遍历；getIdentifier/getXml 直面
#   4) 生命周期链推进至 application.onCreate
# 常规 + ASAN 双过。用法: tools/resource_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_resource_tests"
FIXTURE_OUT="$CPP/tests/out"
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
AAPT2="/Users/cao/Library/Android/sdk/build-tools/36.0.0/aapt2"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "resource_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "resource_tests: d8 not found: $D8" >&2
  exit 1
fi
if [[ ! -x "$AAPT2" ]]; then
  echo "resource_tests: aapt2 not found: $AAPT2" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量）
GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "resource_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
  exit 1
fi
if [ ! -f "$RUNTIME_DEX" ] || [ -n "$(find "$ROOT/runtime-libs/src/main/java" -name '*.java' -newer "$RUNTIME_DEX" 2>/dev/null | head -1)" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "resource_tests: runtime.dex missing after build: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/resource-src -> classes.dex）
echo "==== build fixture (resource.apk) ===="
SRC_DIR="$CPP/tests/resource-src"
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
  echo "resource_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

"$JAVAC" --release 8 -Xlint:-options -encoding UTF-8 -g -classpath "$ANDROID_JAR" -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR" -name '*.class' | sort)
EOF
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "resource_tests: d8 did not produce classes.dex" >&2
  exit 1
fi

# 2. aapt2 compile res/ + link 真 binary manifest/res AXML；zip classes.dex 入 APK
rm -f "$FIXTURE_OUT/resource.apk" "$OUT/res-compiled.zip"
"$AAPT2" compile --dir "$SRC_DIR/res" -o "$OUT/res-compiled.zip"
"$AAPT2" link --manifest "$SRC_DIR/AndroidManifest.xml" "$OUT/res-compiled.zip" \
  -I "$ANDROID_JAR" --min-sdk-version 21 --target-sdk-version 34 \
  -o "$FIXTURE_OUT/resource.apk"
(cd "$D8_DIR" && zip -q "$FIXTURE_OUT/resource.apk" classes.dex)
echo "wrote $FIXTURE_OUT/resource.apk"

# 3. 编译 harness（core VM + framework 内建 + 生命周期驱动器 + session IO）
SOURCES=(
  "$CPP"/core/*.cc
  "$CPP/memory/heap.cc"
  "$CPP/session/session.cc"
  "$CPP/session/class_loader.cc"
  "$CPP/framework/lifecycle_driver.cc"
  "$CPP"/dex/*.cc
  "$CPP/tests/core/resource_smoke.cc"
)

echo "==== compile resource_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/resource_smoke"

echo "==== run resource_smoke (regular) ===="
"$OUT/resource_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/resource.apk"

# 4. ASAN 双过
echo "==== compile resource_smoke (ASAN) ===="
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -fsanitize=address \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/resource_smoke_asan"

echo "==== run resource_smoke (ASAN) ===="
"$OUT/resource_smoke_asan" "$RUNTIME_DEX" "$FIXTURE_OUT/resource.apk"

echo "==== resource tests: PASS ===="
