#!/usr/bin/env bash
# ProviderInfo 装配 + manifest 值投影宿主测试（独立脚本，不动既有 tools/*.sh）。
# fixture 线（tests/provider-src -> tests/out/provider_info.apk：javac+d8 出
# classes.dex + aapt2 打包真 binary AndroidManifest.xml）跑在 provider_info_smoke
# （core VM + framework 内建 + runtime.dex boot + 生命周期驱动器）：
#   1) session 解析投影：ProviderEntry 全字段（authorities 分号多值原串、
#      exported/enabled/multiprocess、process/permission 族、<meta-data> 分型）+
#      versionCode/versionName + <application> <meta-data>
#   2) ProviderInfo 装配全字段（framework_builtins 类面 + BuildProviderInfo
#      值语义：相对名展开/':'-私有进程/permission 回落/intent-filter 缺省
#      exported/metaData 投影/applicationInfo 单例）
#   3) attachInfo 链（fixture provider 覆写读 ProviderInfo 全字段断言）
#   4) AssembleMetaBundle/ProjectPackageInfo（semface 共用投影：Bundle 分型 put、
#      PackageInfo.versionCode/versionName = manifest 真值）
# 常规 + ASAN 双过。用法: tools/provider_info_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_provider_info_tests"
FIXTURE_OUT="$CPP/tests/out"
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
AAPT2="/Users/cao/Library/Android/sdk/build-tools/36.0.0/aapt2"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "provider_info_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "provider_info_tests: d8 not found: $D8" >&2
  exit 1
fi
if [[ ! -x "$AAPT2" ]]; then
  echo "provider_info_tests: aapt2 not found: $AAPT2" >&2
  exit 1
fi

# 0. runtime.dex（boot classpath；gradle 增量——runtime-libs 变更会重打包）
GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "provider_info_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
  exit 1
fi
if [ ! -f "$RUNTIME_DEX" ] || [ -n "$(find "$ROOT/runtime-libs/src/main/java" -name '*.java' -newer "$RUNTIME_DEX" 2>/dev/null | head -1)" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "provider_info_tests: runtime.dex missing after build: $RUNTIME_DEX" >&2
  exit 1
fi

# 1. fixture dex（tests/provider-src -> classes.dex）
echo "==== build fixture (provider_info.apk) ===="
SRC_DIR="$CPP/tests/provider-src"
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
  echo "provider_info_tests: no java sources under $SRC_DIR" >&2
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
  echo "provider_info_tests: d8 did not produce classes.dex" >&2
  exit 1
fi

# 2. binary manifest APK（aapt2 link 真 AXML + zip classes.dex）
rm -f "$FIXTURE_OUT/provider_info.apk"
"$AAPT2" link --manifest "$SRC_DIR/AndroidManifest.xml" -I "$ANDROID_JAR" \
  --min-sdk-version 21 --target-sdk-version 34 -o "$FIXTURE_OUT/provider_info.apk"
(cd "$D8_DIR" && zip -q "$FIXTURE_OUT/provider_info.apk" classes.dex)
echo "wrote $FIXTURE_OUT/provider_info.apk"

# 3. 编译 harness（core VM + framework 内建 + 生命周期驱动器 + session IO）
SOURCES=(
  "$CPP"/core/*.cc
  "$CPP/memory/heap.cc"
  "$CPP/session/session.cc"
  "$CPP/session/class_loader.cc"
  "$CPP/framework/lifecycle_driver.cc"
  "$CPP"/dex/*.cc
  "$CPP/tests/core/provider_info_smoke.cc"
)

echo "==== compile provider_info_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/provider_info_smoke"

echo "==== run provider_info_smoke (regular) ===="
"$OUT/provider_info_smoke" "$RUNTIME_DEX" "$FIXTURE_OUT/provider_info.apk"

# 4. ASAN 双过
echo "==== compile provider_info_smoke (ASAN) ===="
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -fsanitize=address \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/provider_info_smoke_asan"

echo "==== run provider_info_smoke (ASAN) ===="
"$OUT/provider_info_smoke_asan" "$RUNTIME_DEX" "$FIXTURE_OUT/provider_info.apk"

echo "==== provider_info tests: PASS ===="
