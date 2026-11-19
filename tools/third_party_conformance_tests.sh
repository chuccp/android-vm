#!/usr/bin/env bash
# 三方库 conformance 宿主测试（覆盖面参考启发自 v1 NativeVmThirdPartyCoroutineTest.kt
# + RuntimeThirdPartyCoroutineFixture；判据 = 各库文档语义）。
# 可跑集（按 v2 现状裁剪）：Jackson（tree/pretty-print 初始化）、Gson、
# commons-lang3、okio（Buffer sha256；需 kotlin-stdlib）。
# 后置面（不进本次）：Netty/RxJava/fastjson/fastjson2（体量重 + 网络/调度面）、
# kotlinx.coroutines 结构化并发（GuestTask 模型与 v2 协程面另有专测）。
# jar 来源：本机 Gradle 缓存（显式路径；缺任一必需 jar 则整套 SKIP 并注明）。
# 独立脚本（不动既有 tools/*.sh 与 tests 既有文件）。
# 用法: tools/third_party_conformance_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_third_party_tests"
FIXTURE_OUT="$CPP/tests/out"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"
GRADLE_CACHE="$HOME/.gradle/caches/modules-2/files-2.1"

# ---- 必需 jar（显式定位；缺则 SKIP）----
JACKSON_CORE=$(find "$GRADLE_CACHE/com.fasterxml.jackson.core/jackson-core" -name 'jackson-core-2.18*.jar' 2>/dev/null | grep -v sources | grep -v javadoc | head -1 || true)
JACKSON_DB=$(find "$GRADLE_CACHE/com.fasterxml.jackson.core/jackson-databind" -name 'jackson-databind-2.18*.jar' 2>/dev/null | grep -v sources | grep -v javadoc | head -1 || true)
JACKSON_ANN=$(find "$GRADLE_CACHE/com.fasterxml.jackson.core/jackson-annotations" -name 'jackson-annotations-2.18*.jar' 2>/dev/null | grep -v sources | grep -v javadoc | head -1 || true)
GSON=$(find "$GRADLE_CACHE/com.google.code.gson/gson" -name 'gson-2.1*.jar' 2>/dev/null | grep -v sources | grep -v javadoc | head -1 || true)
COMMONS_LANG3=$(find "$GRADLE_CACHE/org.apache.commons/commons-lang3" -name 'commons-lang3-3.17*.jar' 2>/dev/null | grep -v sources | grep -v javadoc | head -1 || true)
OKIO=$(find "$GRADLE_CACHE/com.squareup.okio/okio-jvm" -name 'okio-jvm-3.6*.jar' 2>/dev/null | grep -v sources | grep -v javadoc | head -1 || true)
if [ -z "$OKIO" ]; then
  OKIO=$(find "$GRADLE_CACHE/com.squareup.okio/okio-jvm" -name 'okio-jvm-3.*.jar' 2>/dev/null | grep -v sources | grep -v javadoc | head -1 || true)
fi
KOTLIN_STDLIB=$(find "$GRADLE_CACHE/org.jetbrains.kotlin/kotlin-stdlib" -name 'kotlin-stdlib-1.9*.jar' 2>/dev/null | grep -v sources | grep -v javadoc | sort -V | tail -1 || true)

MISSING=""
for j in "$JACKSON_CORE" "$JACKSON_DB" "$JACKSON_ANN" "$GSON" "$COMMONS_LANG3" "$OKIO" "$KOTLIN_STDLIB"; do
  if [ -z "$j" ]; then MISSING="$MISSING missing"; fi
done
if [ -n "$MISSING" ]; then
  echo "third_party_conformance_tests: SKIP — gradle 缓存缺必需 jar（Jackson/Gson/commons-lang3/okio/kotlin-stdlib）；" >&2
  echo "  需先在联网环境拉取依赖后重跑（可跑集裁剪说明见脚本头注记）" >&2
  exit 0
fi

if [[ ! -x "$JAVAC" || ! -x "$D8" ]]; then
  echo "third_party_conformance_tests: javac/d8 not found" >&2
  exit 1
fi

# runtime.dex（boot classpath）
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
  if [ -z "$GRADLE" ]; then
    echo "third_party_conformance_tests: gradle 9.6.0 dist not found" >&2
    exit 1
  fi
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi

echo "==== jars ===="
echo "  jackson-core:      $JACKSON_CORE"
echo "  jackson-databind:  $JACKSON_DB"
echo "  jackson-ann:       $JACKSON_ANN"
echo "  gson:              $GSON"
echo "  commons-lang3:     $COMMONS_LANG3"
echo "  okio-jvm:          $OKIO"
echo "  kotlin-stdlib:     $KOTLIN_STDLIB"

# 1. fixture dex（tptest + 三方 jar 一并 d8，允许多 dex 输出）
echo "==== build fixture (third_party.dex) ===="
SRC_DIR="$CPP/tests/third_party-src"
CLASSES_DIR="$OUT/classes"
D8_DIR="$OUT/d8-tmp"
rm -rf "$CLASSES_DIR" "$D8_DIR"
mkdir -p "$CLASSES_DIR" "$D8_DIR" "$FIXTURE_OUT"

CP="$JACKSON_CORE:$JACKSON_DB:$JACKSON_ANN:$GSON:$COMMONS_LANG3:$OKIO:$KOTLIN_STDLIB"
"$JAVAC" --release 8 -Xlint:-options -encoding UTF-8 -g -classpath "$CP" \
  -d "$CLASSES_DIR" $(find "$SRC_DIR" -name '*.java' | sort)

JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" \
  $(find "$CLASSES_DIR/tptest" -name '*.class' | sort) \
  "$JACKSON_CORE" "$JACKSON_DB" "$JACKSON_ANN" "$GSON" "$COMMONS_LANG3" \
  "$OKIO" "$KOTLIN_STDLIB"

DEX_ARGS=()
rm -f "$FIXTURE_OUT"/third_party*.dex
for dex in "$D8_DIR"/classes*.dex; do
  [ -f "$dex" ] || continue
  name=$(basename "$dex")
  target="$FIXTURE_OUT/third_party_${name}"
  mv "$dex" "$target"
  DEX_ARGS[${#DEX_ARGS[@]}]="$target"
done
if [ ${#DEX_ARGS[@]} -eq 0 ]; then
  echo "third_party_conformance_tests: d8 produced no dex" >&2
  exit 1
fi
echo "wrote ${DEX_ARGS[*]}"

# 2. 编译 harness（core VM + session）
echo "==== compile third_party_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/third_party_smoke.cc" \
  -lz \
  -o "$OUT/third_party_smoke"

# 3. 执行（FAIL 行/退出码即结果）
echo "==== run third_party_smoke ===="
exec "$OUT/third_party_smoke" "$RUNTIME_DEX" "${DEX_ARGS[@]}"
