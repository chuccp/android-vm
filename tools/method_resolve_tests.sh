#!/usr/bin/env bash
# VM 侧方法解析复现（Math.max NoSuchMethodError / Math.<clinit> 根因）。
# 独立脚本（不动 host_tests.sh）：生成 MathCaller dex（invoke-static Math.max）+
# 编译跑 method_resolve_smoke。断言 Math.max 从 runtime.dex 解析 + Math.<clinit>
# 走 Float/Double 位 intrinsic + 解释器 invoke-static Math.max 端到端。
# 用法: tools/method_resolve_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_method_resolve_tests"
TESTS_OUT="$CPP/tests/out"

RUNTIME_DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
if [ ! -f "$RUNTIME_DEX" ]; then
  echo "method_resolve_tests: runtime.dex missing: $RUNTIME_DEX" >&2
  echo "  先跑 gradle :runtime-libs:build（或 tools/apk_run_tests.sh）" >&2
  exit 1
fi

# ---- 0. 生成 MathCaller.dex（javac + d8）：方法体 invoke-static Ljava/lang/Math;.max(II)I ----
JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"
CALLER_DEX="$TESTS_OUT/mathcaller.dex"
if [ ! -x "$JAVAC" ] || [ ! -x "$D8" ]; then
  echo "method_resolve_tests: javac/d8 not found；跳过解释器 invoke-static 段（仅跑解析+clinit）" >&2
else
  MC_SRC="$OUT/MathCaller.java"
  MC_CLASSES="$OUT/mathcaller-classes"
  MC_DEXDIR="$OUT/mathcaller-dex"
  mkdir -p "$MC_CLASSES" "$MC_DEXDIR"
  cat > "$MC_SRC" <<'EOF'
public class MathCaller {
    public static int callMax(int a, int b) {
        return Math.max(a, b);
    }
    public static int callMaxNoInit() {
        return Math.max(3, 5);
    }
}
EOF
  "$JAVAC" --release 8 -encoding UTF-8 -g -d "$MC_CLASSES" "$MC_SRC" 2>/dev/null
  JAVA_HOME="$JAVA_HOME_DIR" "$D8" --release --lib "$ANDROID_JAR" --output "$MC_DEXDIR" "$MC_CLASSES"/*.class 2>/dev/null
  mkdir -p "$TESTS_OUT"
  cp "$MC_DEXDIR/classes.dex" "$CALLER_DEX"
  echo "method_resolve_tests: generated $CALLER_DEX"
fi

echo "==== compile method_resolve_smoke ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/method_resolve_smoke.cc" \
  -o "$OUT/method_resolve_smoke"

echo "==== run method_resolve_smoke ===="
exec "$OUT/method_resolve_smoke" "$RUNTIME_DEX" "$CALLER_DEX"
