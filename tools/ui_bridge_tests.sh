#!/usr/bin/env bash
# UI 渲染桥接 P1 验收（guest View 树 -> 桥接出画）：
#   1) 宿主冒烟（ui_bridge_smoke）：协议往返、快照/patch 断言、缝A 命令记录、
#      点击回灌（guest listener 派发）、桥轨首帧（target-app 全链）；
#   2) 设备实证（emulator-5554）：adb install + 启动 -> uiautomator dump 断言
#      宿主物化出的真 View 文本（"target ok"）+ 截图 + 点击回灌 logcat；
#   3) 桥接断言轨零回归：tools/render_tests.sh（render_smoke 照常绿）。
# 独立脚本（不动既有 tools/*.sh）。用法: tools/ui_bridge_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_ui_bridge_tests"
DEVICE_SERIAL="${UI_BRIDGE_DEVICE:-emulator-5554}"
ADB="$HOME/Library/Android/sdk/platform-tools/adb"

RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
TARGET_APK="$ROOT/target-app/build/outputs/apk/debug/target-app-debug.apk"
PROBE_DEX="$CPP/tests/out/ui_bridge_fixture.dex"

GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "ui_bridge_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
  exit 1
fi

# 0. 产物构建（缺什么补什么）
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "==== build runtime-libs (runtime.dex) ===="
  "$GRADLE" -p "$ROOT" :runtime-libs:build
fi
if [ ! -f "$TARGET_APK" ]; then
  echo "==== build target-app (target-app-debug.apk) ===="
  "$GRADLE" -p "$ROOT" :target-app:assembleDebug
fi
if [ ! -f "$RUNTIME_DEX" ] || [ ! -f "$TARGET_APK" ]; then
  echo "ui_bridge_tests: runtime.dex / target APK missing after build" >&2
  exit 1
fi

echo "==== build click probe dex ===="
"$ROOT/tools/make_ui_bridge_fixture.sh"

# 1. 编译 + 执行宿主冒烟（协议 TU + 渲染管线 + UI Bridge + 桥接缝）
echo "==== compile ui_bridge_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -DDEXVM_SESSION_IO \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP/framework/lifecycle_driver.cc" \
  "$CPP/framework/render.cc" \
  "$CPP/bridge/bridge.cc" \
  "$CPP/bridge/ui_bridge_protocol.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/ui_bridge_smoke.cc" \
  -lz \
  -o "$OUT/ui_bridge_smoke"

echo "==== run ui_bridge_smoke ===="
"$OUT/ui_bridge_smoke" "$RUNTIME_DEX" "$TARGET_APK" "$PROBE_DEX"

# 2. 设备实证（真 View 树 / 首帧 / 点击）
echo "==== device evidence ($DEVICE_SERIAL) ===="
if ! "$ADB" devices | grep -q "$DEVICE_SERIAL"; then
  echo "ui_bridge_tests: device $DEVICE_SERIAL not present (adb devices)" >&2
  echo "FAIL: device evidence required (uiautomator dump must show bridged real views)" >&2
  exit 1
fi

echo "==== build host app (libdexvm + materializer) ===="
"$GRADLE" -p "$ROOT" :app:assembleDebug
HOST_APK="$ROOT/app/build/outputs/apk/debug/app-debug.apk"
if [ ! -f "$HOST_APK" ]; then
  # 产物名兜底（AGP 可能带 -debug 后缀差异）
  HOST_APK="$(ls "$ROOT"/app/build/outputs/apk/debug/*.apk | head -1)"
fi
echo "host apk: $HOST_APK"

echo "==== adb install + launch ===="
"$ADB" -s "$DEVICE_SERIAL" install -r "$HOST_APK" >/dev/null
"$ADB" -s "$DEVICE_SERIAL" logcat -c || true
"$ADB" -s "$DEVICE_SERIAL" shell am start -n com.dexvm.host/.MainActivity >/dev/null
sleep 3
# 桌面 launcher：点「Minimal Android」（target-app 导入格）打开 guest 会话
"$ADB" -s "$DEVICE_SERIAL" shell input tap 73 166

echo "==== 等待 guest 首帧物化（会话模式，最多 90s） ===="
DUMP_FLAT=""
for _ in $(seq 1 30); do
  sleep 3
  "$ADB" -s "$DEVICE_SERIAL" shell uiautomator dump /sdcard/ui_bridge_win.xml >/dev/null 2>&1
  DUMP="$("$ADB" -s "$DEVICE_SERIAL" shell cat /sdcard/ui_bridge_win.xml)"
  DUMP_FLAT="$(echo "$DUMP" | tr -d '\r')"
  if echo "$DUMP_FLAT" | grep -q "XML Runtime Layout"; then
    break
  fi
done
echo "$DUMP_FLAT" | head -c 600
echo

check_dump() {
  if echo "$DUMP_FLAT" | grep -q "$1"; then
    echo "PASS: dump contains '$1'"
  else
    echo "FAIL: dump missing '$1'"
    return 1
  fi
}
DUMP_FAIL=0
# 资源布局直跑线首帧（runtime_primary.xml 膨胀后的真实 guest 文本）
check_dump "XML Runtime Layout" || DUMP_FAIL=1
# 宿主物化出的真 TextView 类名（uiautomator class 属性）
check_dump "android.widget.TextView" || DUMP_FAIL=1
# 物化器日志（rev/patch/dirty）
if "$ADB" -s "$DEVICE_SERIAL" logcat -d -s DexVmUiBridge:I | grep -q "materialized rev="; then
  echo "PASS: materializer log (rev/patch/dirty) present"
else
  echo "FAIL: materializer log missing"
  DUMP_FAIL=1
fi
if [ "$DUMP_FAIL" -ne 0 ]; then
  echo "FAIL: bridged real View tree not visible in uiautomator dump" >&2
  exit 1
fi

echo "==== screenshot ===="
"$ADB" -s "$DEVICE_SERIAL" exec-out screencap -p > "$OUT/ui_bridge_screen.png"
ls -la "$OUT/ui_bridge_screen.png"

echo "==== click injection（host tap -> viewId -> guest 通路） ===="
# 从 dump 解析「Tap the virtual layout」按钮 bounds，点其中心（点击回灌）
TAP_XY="$(echo "$DUMP_FLAT" | python3 -c "
import re, sys
s = sys.stdin.read()
for m in re.finditer(r'<node[^>]*>', s):
    if 'Tap the virtual layout' in m.group(0):
        b = re.search(r'bounds=\"\[(\d+),(\d+)\]\[(\d+),(\d+)\]\"', m.group(0))
        if b:
            x1, y1, x2, y2 = map(int, b.groups())
            print((x1 + x2) // 2, (y1 + y2) // 2)
        break
")"
if [ -z "$TAP_XY" ]; then
  echo "FAIL: cannot locate clickable guest button bounds for tap" >&2
  exit 1
fi
echo "tap $TAP_XY"
"$ADB" -s "$DEVICE_SERIAL" shell input tap $TAP_XY
sleep 2
CLICK_LOG="$("$ADB" -s "$DEVICE_SERIAL" logcat -d -s DexVmUiBridge:I)"
echo "$CLICK_LOG" | tail -5
if echo "$CLICK_LOG" | grep -q "host click viewId="; then
  echo "PASS: host click reached bridge channel (viewId -> native)"
else
  echo "FAIL: no host click log (DexVmUiBridge)" >&2
  exit 1
fi
if echo "$CLICK_LOG" | grep -q "materialized rev="; then
  echo "PASS: materializer log (rev/patch/dirty) present"
else
  echo "FAIL: materializer log missing" >&2
  exit 1
fi

# 3. 桥接断言轨零回归（render_smoke 照常绿）
echo "==== bridge-track regression (render_tests) ===="
"$ROOT/tools/render_tests.sh"

echo "UI BRIDGE TESTS ALL PASS"
