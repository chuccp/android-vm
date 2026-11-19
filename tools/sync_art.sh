#!/usr/bin/env bash
# ART 源码 vendor 同步脚本（文档 §2 ART 基线 / §3 目录结构）
#
# 用法: tools/sync_art.sh [ART_SRC]
#   ART_SRC  ART 源码 checkout 路径，默认 /Users/cao/Downloads/art
#
# 按清单把 ART 基线源码同步到 app/src/main/cpp/third_party/art/，
# 并写 UPSTREAM.lock 记录 commit 与同步范围。同步范围可随时在
# SYNC_MANIFEST 里扩展（如 D7 需要 test/）。
set -euo pipefail

ART_SRC="${1:-/Users/cao/Downloads/art}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$REPO_ROOT/app/src/main/cpp/third_party/art"

# 需要 vendor 的子树/文件（对齐文档 §3 third_party/art 清单 + 构建支撑）
SYNC_MANIFEST=(
  libartbase
  libdexfile
  libelffile
  libprofile
  libartpalette
  libnativebridge
  libnativeloader
  sigchainlib
  cmdline
  compiler
  runtime
  tools
  odrefresh
  build
  openjdkjvm
  openjdkjvmti
  Android.bp
  METADATA
  NOTICE
  LICENSE
)

if [[ ! -d "$ART_SRC" ]]; then
  echo "ART 源码不存在: $ART_SRC" >&2
  exit 1
fi

COMMIT="$(git -C "$ART_SRC" rev-parse HEAD)"

mkdir -p "$DEST"
for entry in "${SYNC_MANIFEST[@]}"; do
  if [[ -e "$ART_SRC/$entry" ]]; then
    rsync -a --delete "$ART_SRC/$entry" "$DEST/"
    echo "synced: $entry"
  else
    echo "skip (not found): $entry" >&2
  fi
done

{
  echo "upstream: ART"
  echo "source: $ART_SRC"
  echo "commit: $COMMIT"
  echo "branch: 26Q2-release"
  echo "synced_at: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "manifest:"
  printf '  - %s\n' "${SYNC_MANIFEST[@]}"
} > "$DEST/UPSTREAM.lock"

echo "done -> $DEST/UPSTREAM.lock (commit $COMMIT)"
