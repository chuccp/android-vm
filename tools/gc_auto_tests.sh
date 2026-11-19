#!/usr/bin/env bash
# 自动 GC 接线冒烟（独立；docs 12 章 22.10 AllocateInternalWithGc）：
#   1) 分配压力：对象流 > 容量、存活集保持（分配失败→GC 重试链救回）
#   2) OOM 语义：可达内存耗尽抛真 OutOfMemoryError 类（预分配单例）
#   3) GC 后引用完整性（gc_smoke 语义）
#   4) 多 GuestTask 并发分配（fiber 栈保守扫描保活挂起 task 局部引用）
# 用法: tools/gc_auto_tests.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_gc_auto_tests"

mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -pthread \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/coroutine/guest_task.cc" \
  "$CPP/coroutine/scheduler.cc" \
  "$CPP/coroutine/fiber_switch.S" \
  "$CPP/tests/core/gc_auto_smoke.cc" \
  -o "$OUT/gc_auto_smoke"
exec "$OUT/gc_auto_smoke"
