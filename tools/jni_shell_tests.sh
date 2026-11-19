#!/usr/bin/env bash
# "壳形态" JNI 语义宿主测试（覆盖面参考启发自 v1 hardened_fixture.cpp/
# HardenedNativeFixture/GuestNativeLibraryTest.kt 对抗手法清单；断言按
# Android/JNI 语义）。覆盖：System.load -> JNI_OnLoad（构造 cookie/dladdr）->
# Java_* 解析 register -> RegisterNatives -> mixed/decrypted/byte[] 变换/
# null ThrowNew/IllegalArgument ThrowNew/CallStaticVoidMethod 回调/跨库
# dladdr 完整性/TracerPid 探测 + 可选真实商业 .so 加载（libbangcle 形态）。
# 独立脚本（不动既有 tools/*.sh 与 tests 既有文件）。
# 用法: tools/jni_shell_tests.sh [real_vendor.so]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_jni_shell_tests"
FIXTURE_OUT="$CPP/tests/out"
REAL_SO="${1:-}"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"
NDK_INCLUDE="/Users/cao/Library/Android/sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/include"

if [[ ! -x "$JAVAC" ]]; then
  echo "jni_shell_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "jni_shell_tests: d8 not found: $D8" >&2
  exit 1
fi
if [[ ! -f "$NDK_INCLUDE/jni.h" ]]; then
  echo "jni_shell_tests: NDK jni.h not found: $NDK_INCLUDE/jni.h" >&2
  exit 1
fi

# runtime.dex（boot classpath）
RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
  if [ -z "$GRADLE" ]; then
    echo "jni_shell_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
    exit 1
  fi
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi

mkdir -p "$OUT" "$FIXTURE_OUT"

# 1. jni.h staging（真实 <jni.h>；jni_full_tests.sh 同口径）
JNI_STAGE="$OUT/jni_include"
mkdir -p "$JNI_STAGE"
cp "$NDK_INCLUDE/jni.h" "$JNI_STAGE/jni.h"

# 2. fixture dex（tests/jni_shell-src -> tests/out/jni_shell.dex）
echo "==== build fixture (jni_shell.dex) ===="
SRC_DIR="$CPP/tests/jni_shell-src"
CLASSES_DIR="$OUT/classes"
D8_DIR="$OUT/d8-tmp"
rm -rf "$CLASSES_DIR" "$D8_DIR"
mkdir -p "$CLASSES_DIR" "$D8_DIR"

JAVA_SOURCES=()
while IFS= read -r f; do
  JAVA_SOURCES[${#JAVA_SOURCES[@]}]="$f"
done <<EOF
$(find "$SRC_DIR" -name '*.java' | sort)
EOF
if [ ${#JAVA_SOURCES[@]} -eq 0 ]; then
  echo "jni_shell_tests: no java sources under $SRC_DIR" >&2
  exit 1
fi

"$JAVAC" --release 8 -Xlint:-options -encoding UTF-8 -g -d "$CLASSES_DIR" "${JAVA_SOURCES[@]}"

CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR/shelltest" -name '*.class' | sort)
EOF
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
if [ ! -f "$D8_DIR/classes.dex" ]; then
  echo "jni_shell_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/jni_shell.dex"
echo "wrote $FIXTURE_OUT/jni_shell.dex"

# 3. 测试 .so ×2（payload 跨库依赖 + fixture 壳形态）
echo "==== build shell native .so ===="
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -shared -fPIC \
  -install_name @loader_path/libshell_payload.so \
  -I "$JNI_STAGE" \
  "$CPP/tests/jni_shell-src/shell_payload.cpp" \
  -o "$OUT/libshell_payload.so"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -shared -fPIC \
  -I "$JNI_STAGE" \
  "$CPP/tests/jni_shell-src/shell_fixture.cpp" \
  "$OUT/libshell_payload.so" \
  -Wl,-rpath,@loader_path \
  -o "$OUT/libshell_fixture.so"
echo "built $OUT/libshell_fixture.so (+ libshell_payload.so)"

# 4. 编译 harness（core VM + jni + session）
echo "==== compile jni_shell_smoke ===="
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP"/dex/*.cc \
  "$CPP"/jni/handle_table.cc \
  "$CPP"/jni/jni_env.cc \
  "$CPP"/jni/native_manager.cc \
  "$CPP"/jni/reflection.cc \
  "$CPP/tests/core/jni_shell_smoke.cc" \
  -lz \
  -o "$OUT/jni_shell_smoke"

# 5. 执行（FAIL 行/退出码即结果；cwd = 仓库根）
echo "==== run jni_shell_smoke ===="
cd "$ROOT"
if [ -n "$REAL_SO" ]; then
  exec "$OUT/jni_shell_smoke" "$FIXTURE_OUT/jni_shell.dex" "$OUT/libshell_fixture.so" "$REAL_SO"
fi
exec "$OUT/jni_shell_smoke" "$FIXTURE_OUT/jni_shell.dex" "$OUT/libshell_fixture.so"
