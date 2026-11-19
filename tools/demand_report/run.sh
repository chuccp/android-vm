#!/usr/bin/env bash
# 知乎考卷 android.* 需求面提取（可复跑）：
#   扫 APK 全部 dex -> 类/成员/引用频次清单 + 与现有面差集（缺口清单）。
# 产物：tools/demand_report/{report.txt,demand_classes.tsv,demand_members.tsv}
# 用法: tools/demand_report/run.sh [apk]   （默认 /tmp/target-apks/zhihu.apk）
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
APK="${1:-/tmp/target-apks/zhihu.apk}"
exec python3 -I "$HERE/extract_android_demand.py" --apk "$APK" --out "$HERE"
