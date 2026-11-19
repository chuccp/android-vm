#!/usr/bin/env bash
# JNI 全面（M2）宿主测试：
#   1. ABI 槽位对照：NDK <jni.h> vs jni/jni_env.h 的 JNINativeInterface 同名字段
#      逐槽 offsetof 校验（真实 .so ABI 兼容的机械证明）
#   2. tests/fixture-src/fixture_native.c -> 真实 <jni.h> C ABI 测试 .so
#   3. tests/core/jni_full_smoke.cc 全量冒烟（RegisterNatives/loadLibrary/反射/
#      Call 族/Critical/EnsureLocalCapacity 等）
# 独立脚本（不改 tools/host_tests.sh）。
# 用法: tools/jni_full_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_jni_full_tests"
NDK_INCLUDE="/Users/cao/Library/Android/sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/include"

mkdir -p "$OUT"

# fixture.dex（fixture 类 + loadLibrary 内 FindClass 用）
if [ ! -f "$CPP/tests/out/fixture.dex" ]; then
  "$ROOT/tools/make_fixture.sh"
fi

# ---------- 1. ABI 槽位对照 ----------
echo "==== jni ABI slot check ===="
if [ ! -f "$NDK_INCLUDE/jni.h" ]; then
  echo "jni_full_tests: NDK jni.h not found: $NDK_INCLUDE/jni.h" >&2
  exit 1
fi
# 只取 jni.h 自身（NDK sysroot include 整目录会遮蔽宿主 libc 头）
JNI_STAGE="$OUT/jni_include"
mkdir -p "$JNI_STAGE"
cp "$NDK_INCLUDE/jni.h" "$JNI_STAGE/jni.h"

# 从 jni_env.h 提取 JNINativeInterface 成员名（reserved0-3 + (JNICALL* Name)）
FIELDS=$(sed -n '/^struct JNINativeInterface {/,/^};/p' "$CPP/jni/jni_env.h" \
  | grep -oE '(JNICALL\* [A-Za-z_][A-Za-z0-9_]*|void\* reserved[0-9])' \
  | sed -e 's/JNICALL\* //' -e 's/void\* //')
if [ -z "$FIELDS" ]; then
  echo "jni_full_tests: failed to extract JNINativeInterface fields" >&2
  exit 1
fi

OFFLIST="$OUT/offlist.inc"
: > "$OFFLIST"
for f in $FIELDS; do
  printf '  O(%s)\n' "$f" >> "$OFFLIST"
done

gen_offsets() {
  local inc="$1"
  cat > "$2" <<EOF
#include <cstddef>
#include <cstdio>
$inc
#define O(f) std::printf("%s %zu\\n", #f, offsetof(JNINativeInterface, f));
int main() {
#include "$OFFLIST"
#undef O
  return 0;
}
EOF
}

gen_offsets '#include <jni.h>' "$OUT/offsets_sys.cc"
gen_offsets '#include "jni/jni_env.h"' "$OUT/offsets_ours.cc"

xcrun clang++ -std=c++20 -I "$JNI_STAGE" "$OUT/offsets_sys.cc" -o "$OUT/offsets_sys"
xcrun clang++ -std=c++20 -I "$CPP" "$OUT/offsets_ours.cc" -o "$OUT/offsets_ours"
"$OUT/offsets_sys" > "$OUT/offsets_sys.txt"
"$OUT/offsets_ours" > "$OUT/offsets_ours.txt"
if ! diff -u "$OUT/offsets_sys.txt" "$OUT/offsets_ours.txt"; then
  echo "FAIL: JNINativeInterface slot layout mismatch vs NDK jni.h" >&2
  exit 1
fi
echo "PASS: JNINativeInterface slots match NDK jni.h ($(wc -l < "$OUT/offsets_sys.txt" | tr -d ' ') slots)"

# ---------- 2. 测试 .so（真实 <jni.h> C ABI）----------
echo "==== fixture native .so ===="
xcrun clang -shared -fPIC -Wl,-undefined,dynamic_lookup \
  -I "$JNI_STAGE" \
  "$CPP/tests/fixture-src/fixture_native.c" \
  -o "$OUT/libfixture_native.so"
echo "built $OUT/libfixture_native.so"

# ---------- 3. jni_full_smoke ----------
echo "==== jni full smoke ===="
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP/jni/handle_table.cc" \
  "$CPP/jni/jni_env.cc" \
  "$CPP/jni/native_manager.cc" \
  "$CPP/jni/reflection.cc" \
  "$CPP"/core/*.cc \
  "$CPP/session/class_loader.cc" \
  "$CPP/session/session.cc" \
  "$CPP/memory/heap.cc" \
  "$CPP/dex/dex_file.cc" \
  "$CPP/dex/dex_verify.cc" \
  "$CPP/dex/leb128.cc" \
  "$CPP/dex/mutf8.cc" \
  "$CPP/dex/opcodes.cc" \
  "$CPP/tests/core/jni_full_smoke.cc" \
  -lz \
  -Wl,-export_dynamic \
  -o "$OUT/jni_full_smoke"

exec "$OUT/jni_full_smoke" "$CPP/tests/out/fixture.dex" "$OUT/libfixture_native.so"
