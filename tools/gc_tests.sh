#!/usr/bin/env bash
# GC 冒烟测试
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_gc_tests"

mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/gc_smoke.cc" \
  -o "$OUT/gc_smoke"
exec "$OUT/gc_smoke"
