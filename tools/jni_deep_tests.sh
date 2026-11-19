#!/usr/bin/env bash
# JNI 桥接语义压力（SecNeo 解密回调形态）——按【带终止条件的正常调用循环】
# 构造（用户定性：应用不会合法深递归；深栈/不终止 = 语义偏差实证）：
#   1) 对象链终止循环（每圈跨 JNI 边界；有限圈必须终止 + 栈漂移必须为零
#      + 返回句柄每笔新 slot 单调，真 ART IndirectRef 形态）
#   2) 字符串语义回环（Android MUTF-8 逐字节：空串/中文/内嵌 NUL/增补平面）
#   3) 接口派发终止循环（FindInterfaceImpl 派发回环怀疑面）
#   4) local ref 代回绕误判（悬挂 jobject = 纯语义，与栈无关）
#   5) local ref 耗尽语义（null + OutOfMemoryError）
#   6) 深递归护栏（配套缓解验证：SOE 可观测停点，非根因结论）
#   7) 调度器预算中止归因（非信号失败可归因，不报 "unknown signal"）
#   8) 真形态 token 清单遍历（H.d 四步 + 缓存命中快路径 + GetMethodID 一次；
#      N=5/500/5000 精确终止 + 重复 token 不打转 + 返回句柄新 slot 单调 +
#      IsSameObject canonical 对照 + golden trace（/tmp/zhihu_oracle enc->plain 对））
#   9) <clinit> 失败 erroneous 语义（Android：不再重跑；失败 <clinit> 重跑 =
#      字面量解密清单原地打转的机制来源）
# 常规 + ASAN 双过。独立脚本（不动 tools/host_tests.sh 与既有 tools/tests）。
# 用法: tools/jni_deep_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_jni_deep_tests"
FIXTURE_OUT="$CPP/tests/out"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" ]]; then
  echo "jni_deep_tests: javac not found: $JAVAC" >&2
  exit 1
fi
if [[ ! -x "$D8" ]]; then
  echo "jni_deep_tests: d8 not found: $D8" >&2
  exit 1
fi

# 1. fixture dex（tests/jni_deep-src -> tests/out/jni_deep.dex）
echo "==== build fixture (jni_deep.dex) ===="
SRC_DIR="$CPP/tests/jni_deep-src"
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
  echo "jni_deep_tests: no java sources under $SRC_DIR" >&2
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
  echo "jni_deep_tests: d8 did not produce classes.dex" >&2
  exit 1
fi
mv "$D8_DIR/classes.dex" "$FIXTURE_OUT/jni_deep.dex"
echo "wrote $FIXTURE_OUT/jni_deep.dex"

# 2. 编译 harness（core VM + jni + coroutine + session）
echo "==== compile jni_deep_smoke ===="
mkdir -p "$OUT"
SOURCES=(
  "$CPP"/core/*.cc
  "$CPP/memory/heap.cc"
  "$CPP/session/session.cc"
  "$CPP/session/class_loader.cc"
  "$CPP"/jni/handle_table.cc
  "$CPP"/jni/jni_env.cc
  "$CPP"/jni/native_manager.cc
  "$CPP"/jni/reflection.cc
  "$CPP"/dex/dex_file.cc
  "$CPP"/dex/dex_verify.cc
  "$CPP"/dex/leb128.cc
  "$CPP"/dex/mutf8.cc
  "$CPP"/dex/opcodes.cc
  "$CPP/coroutine/guest_task.cc"
  "$CPP/coroutine/scheduler.cc"
  "$CPP/coroutine/fiber_switch.S"
  "$CPP/tests/core/jni_deep_smoke.cc"
)

xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -pthread \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/jni_deep_smoke"

echo "==== run jni_deep_smoke (regular) ===="
"$OUT/jni_deep_smoke" "$FIXTURE_OUT/jni_deep.dex"

# 3. ASAN 双过
echo "==== compile jni_deep_smoke (ASAN) ===="
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -pthread \
  -fsanitize=address \
  -I "$CPP" \
  "${SOURCES[@]}" \
  -lz \
  -o "$OUT/jni_deep_smoke_asan"

echo "==== run jni_deep_smoke (ASAN) ===="
ASAN_OPTIONS=detect_stack_use_after_return=0:abort_on_error=0 \
  "$OUT/jni_deep_smoke_asan" "$FIXTURE_OUT/jni_deep.dex"

echo "==== jni deep tests: ALL PASS ===="
