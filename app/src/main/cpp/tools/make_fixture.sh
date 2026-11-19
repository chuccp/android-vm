#!/usr/bin/env bash
# 转发到仓库根 tools/make_fixture.sh（支持从 app/src/main/cpp 直接调用）。
exec "$(cd "$(dirname "$0")/../../../../.." && pwd)/tools/make_fixture.sh" "$@"
