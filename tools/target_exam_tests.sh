#!/usr/bin/env bash
# target-app 考卷 8 轮编排——宿主 + 设备跑卷。
# 宿主：apk_run 引导链 + tests/target_exam_table.h 哨兵契约表（R1-R6 表驱动；
#   R7/R8 设备轮，宿主 SKIP 注明）。
# 设备：gradle :app:assembleDebug -> adb install -> am start --ez run_target_exam
#   -> logcat（tag=DexVmTargetExam）逐轮判定；R7 接 UI 桥接出画断言
#   （tools/ui_bridge_tests.sh 先例：uiautomator dump 真 View 树）；R8 加固 .so
#   JNI 收官（依赖链 dlopen/JNI_OnLoad/RegisterNatives a-g/ThrowNew/回调 0x4f21）。
# 用法:
#   tools/target_exam_tests.sh              # 全 8 轮（宿主 + 设备）
#   tools/target_exam_tests.sh host         # 只跑宿主表（R1-R6）
#   tools/target_exam_tests.sh device       # 只跑设备 8 轮
#   ROUND=R3 tools/target_exam_tests.sh device   # 设备单轮
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_target_exam_tests"
DEVICE_SERIAL="${TARGET_EXAM_DEVICE:-emulator-5554}"
ADB="$HOME/Library/Android/sdk/platform-tools/adb"

RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
TARGET_APK="$ROOT/target-app/build/outputs/apk/debug/target-app-debug.apk"

MODE="${1:-all}"
ROUND_SEL="${ROUND:-}"

GRADLE="$(ls "$HOME"/.gradle/wrapper/dists/gradle-9.6.0-bin/*/gradle-9.6.0/bin/gradle 2>/dev/null | head -1 || true)"
if [ -z "$GRADLE" ]; then
  echo "target_exam_tests: gradle 9.6.0 dist not found under ~/.gradle/wrapper/dists" >&2
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
  echo "target_exam_tests: runtime.dex / target APK missing after build" >&2
  exit 1
fi

run_host() {
  local host_rounds="${ROUND_SEL:-1,2,3,4,5,6}"
  echo "==== compile target_exam_smoke (host) ===="
  mkdir -p "$OUT"
  xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
    -DDEXVM_SESSION_IO \
    -pthread \
    -I "$CPP" \
    "$CPP"/core/*.cc \
    "$CPP/memory/heap.cc" \
    "$CPP/session/session.cc" \
    "$CPP/session/class_loader.cc" \
    "$CPP/coroutine/guest_task.cc" \
    "$CPP/coroutine/scheduler.cc" \
    "$CPP/coroutine/fiber_switch.S" \
    "$CPP"/dex/*.cc \
    "$CPP/tests/core/target_exam_smoke.cc" \
    -lz \
    -o "$OUT/target_exam_smoke"

  echo "==== run target_exam_smoke (host, rounds $host_rounds) ===="
  "$OUT/target_exam_smoke" "$RUNTIME_DEX" "$TARGET_APK" "$host_rounds"
}

run_device() {
  echo "==== device evidence ($DEVICE_SERIAL) ===="
  if ! "$ADB" devices | grep -q "$DEVICE_SERIAL"; then
    echo "target_exam_tests: device $DEVICE_SERIAL not present (adb devices)" >&2
    return 1
  fi

  echo "==== build host app (libdexvm + exam entries) ===="
  "$GRADLE" -p "$ROOT" :app:assembleDebug
  HOST_APK="$ROOT/app/build/outputs/apk/debug/app-debug.apk"
  if [ ! -f "$HOST_APK" ]; then
    HOST_APK="$(ls "$ROOT"/app/build/outputs/apk/debug/*.apk | head -1)"
  fi
  echo "host apk: $HOST_APK"

  echo "==== adb install + launch target exam ===="
  "$ADB" -s "$DEVICE_SERIAL" install -r "$HOST_APK" >/dev/null
  "$ADB" -s "$DEVICE_SERIAL" logcat -c || true
  local extra=""
  if [ -n "$ROUND_SEL" ]; then
    extra="--es exam_round $ROUND_SEL"
  fi
  # 宿主 assets copyAsset("target-app-debug.apk") -> files/ -> VM 引导
  "$ADB" -s "$DEVICE_SERIAL" shell am start -n com.dexvm.host/.MainActivity \
    --ez run_target_exam true $extra >/dev/null

  # 8 轮全卷（含 netty 服务 1s 自闭 / 协程 / 加固 .so）预计 ~1-3 分钟
  local waited=0
  local log=""
  while [ "$waited" -lt 300 ]; do
    sleep 10
    waited=$((waited + 10))
    log="$("$ADB" -s "$DEVICE_SERIAL" logcat -d -s DexVmTargetExam:I 2>/dev/null || true)"
    if echo "$log" | grep -q "TARGET EXAM DONE"; then
      break
    fi
  done
  log="$("$ADB" -s "$DEVICE_SERIAL" logcat -d -s DexVmTargetExam:I 2>/dev/null || true)"
  mkdir -p "$OUT"
  echo "$log" > "$OUT/device_report.log"
  echo "$log" | tail -80

  if ! echo "$log" | grep -q "TARGET EXAM DONE"; then
    echo "FAIL: device exam did not finish in ${waited}s (log: $OUT/device_report.log)" >&2
    return 1
  fi

  echo "==== per-round verdicts (device) ===="
  local dev_fail=0
  for r in 1 2 3 4 5 6 7 8; do
    [ -n "$ROUND_SEL" ] && [ "$ROUND_SEL" != "all" ] && [ ",$ROUND_SEL," != *",$r,"* ] && [ "R$r" != "$ROUND_SEL" ] && [ "$r" != "$ROUND_SEL" ] && continue
    # logcat 行带 "MM-DD time pid tid I DexVmTargetExam: " 前缀，不得锚定行首
    if echo "$log" | grep -q "\[R$r\] PASS"; then
      echo "$log" | grep "\[R$r\] PASS" | tail -1
    else
      echo "$log" | grep "\[R$r\]" | tail -1 || echo "[R$r] MISSING"
      dev_fail=1
    fi
  done
  if [ "$dev_fail" -ne 0 ]; then
    echo "FAIL: some device rounds not green (report: $OUT/device_report.log)" >&2
    return 1
  fi

  # R7 桥接出画断言（uiautomator 真 View 树；ui_bridge_tests.sh 先例）
  if [ -z "$ROUND_SEL" ] || [ "$ROUND_SEL" = "all" ] || [ "$ROUND_SEL" = "7" ] || [ "$ROUND_SEL" = "R7" ]; then
    echo "==== R7 bridge out (uiautomator dump) ===="
    "$ADB" -s "$DEVICE_SERIAL" shell uiautomator dump /sdcard/target_exam_win.xml >/dev/null
    local dump
    dump="$("$ADB" -s "$DEVICE_SERIAL" shell cat /sdcard/target_exam_win.xml | tr -d '\r')"
    echo "$dump" | head -c 600
    echo
    local dump_fail=0
    if echo "$dump" | grep -q "target ok\|Programmatic VM panel\|Guest files\|runtime_status\|target-app\|Runtime"; then
      echo "PASS: guest tree materialized on device"
    else
      echo "FAIL: no guest view text in uiautomator dump" >&2
      dump_fail=1
    fi
    if echo "$dump" | grep -q "android.widget.TextView"; then
      echo "PASS: real android.widget.TextView present"
    else
      echo "FAIL: no materialized TextView" >&2
      dump_fail=1
    fi
    "$ADB" -s "$DEVICE_SERIAL" exec-out screencap -p > "$OUT/target_exam_screen.png" || true
    ls -la "$OUT/target_exam_screen.png" 2>/dev/null || true
    if [ "$dump_fail" -ne 0 ]; then
      return 1
    fi
  fi

  echo "DEVICE TARGET EXAM ALL ROUNDS GREEN"
}

case "$MODE" in
  host) run_host ;;
  device) run_device ;;
  all)
    run_host
    run_device
    ;;
  *)
    echo "usage: tools/target_exam_tests.sh [host|device|all]" >&2
    exit 2
    ;;
esac

echo "TARGET EXAM TESTS ALL PASS"
