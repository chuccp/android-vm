#!/usr/bin/env bash
# 扩展覆盖面矩阵统一入口（v1 覆盖面参考桥接；测试本体按 v2 规格写）：
#   1. api_matrix        — 解释器/API 流矩阵 + clinit 挂起/不重放四连 +
#                          WeakReference/GC 面 + java.io 文件面
#   2. concurrency_matrix — 并发原语矩阵（中断/锁条件/同步器/执行器/队列/组）
#   3. jni_shell          — "壳形态" JNI 语义（加载链/RegisterNatives/ThrowNew/
#                           回调/dladdr 完整性/可选真实商业 .so）
#   4. third_party        — 三方库 conformance（Jackson/Gson/commons-lang3/okio）
# 各套独立脚本可单独运行；本入口按序执行并汇总。
# 已知 VM 缺口以 GAP 行呈现（不计 FAIL；详见各套输出）。
# 用法: tools/coverage_matrix_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

run_suite() {
  local name="$1"
  local script="$2"
  echo ""
  echo "################ $name ################"
  "$script"
}

run_suite "api matrix" "$ROOT/tools/api_matrix_tests.sh"
run_suite "concurrency matrix" "$ROOT/tools/concurrency_matrix_tests.sh"
run_suite "jni shell" "$ROOT/tools/jni_shell_tests.sh"
run_suite "third party conformance" "$ROOT/tools/third_party_conformance_tests.sh"

echo ""
echo "coverage matrix: ALL SUITES PASS"
