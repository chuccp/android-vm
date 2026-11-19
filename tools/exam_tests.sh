#!/usr/bin/env bash
# M3 加固考卷：知乎 APK 语义保真验证（零壳适配）。
# boot classpath = runtime-libs/build/runtime-dex/runtime.dex，
# 考卷 = /tmp/target-apks/zhihu.apk（可用 ZHIHU_APK 覆盖）；
# LifecycleDriver 驱动启动链，逐 phase 记录 + 结构化缺口报告。
# 独立脚本（不动既有 tools/*.sh 与 tests 既有文件）。
# 用法: tools/exam_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_exam_tests"

RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
ZHIHU_APK="${ZHIHU_APK:-/tmp/target-apks/zhihu.apk}"
REPORT="$CPP/tests/out/zhihu_exam_report.txt"

# gradle 9.6.0（wrapper dist；local.properties/sdk.dir 已配）
GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "exam_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
  exit 1
fi

# 0. 产物（gradle 增量；缺什么补什么）
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "exam_tests: runtime.dex missing after build: $RUNTIME_DEX" >&2
  exit 1
fi
if [ ! -f "$ZHIHU_APK" ]; then
  echo "exam_tests: target APK not found: $ZHIHU_APK" >&2
  exit 1
fi

# 1. 编译（core 内建 + framework 内建 + 生命周期驱动器 + session IO + zlib）
echo "==== compile exam_smoke ===="
mkdir -p "$OUT"
mkdir -p "$CPP/tests/out"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP/framework/lifecycle_driver.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/exam_smoke.cc" \
  -lz \
  -o "$OUT/exam_smoke"

# 2. 宿主 native 库目录（System.loadLibrary 语义查找面；docs 07 章 16.2）：
#    考卷 APK lib/<abi>/*.so 提取到本地 nativeLibraryDir 供 dlopen 尝试。
NATIVE_DIR="$OUT/native_libs"
rm -rf "$NATIVE_DIR"
mkdir -p "$NATIVE_DIR"
for abi in arm64-v8a armeabi-v7a x86_64; do
  unzip -o -q -j "$ZHIHU_APK" "lib/$abi/*.so" -d "$NATIVE_DIR" 2>/dev/null || true
done

# 3. 执行（报告：stdout + tests/out/zhihu_exam_report.txt）
echo "==== run exam_smoke (zhihu) ===="
exec "$OUT/exam_smoke" "$RUNTIME_DEX" "$ZHIHU_APK" "$REPORT" "$NATIVE_DIR"
