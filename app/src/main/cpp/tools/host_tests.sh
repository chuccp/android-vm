#!/usr/bin/env bash
# 转发到仓库根 tools/host_tests.sh（支持从 app/src/main/cpp 直接调用）。
exec "$(cd "$(dirname "$0")/../../../../.." && pwd)/tools/host_tests.sh" "$@"
