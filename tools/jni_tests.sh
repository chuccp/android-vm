#!/usr/bin/env bash
# JNI 最小面（M1）宿主测试：编译并跑 tests/core/jni_smoke.cc。
# 独立脚本（不改 tools/host_tests.sh / core_tests.sh）。
# 用法: tools/jni_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_jni_tests"

# fixture（jni_smoke 复用 tests/fixture-src 产物 fixture.dex）
if [ ! -f "$CPP/tests/out/fixture.dex" ]; then
  "$ROOT/tools/make_fixture.sh"
fi

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
  "$ROOT/app/src/main/cpp/tests/core/jni_smoke.cc" \
  -lz \
  -o "$OUT"

exec "$OUT" "$CPP/tests/out/fixture.dex"
