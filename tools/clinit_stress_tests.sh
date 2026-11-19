#!/usr/bin/env bash
# M4.3：clinit 深递归链压力测试（SIGSEGV 脏 vreg 复现面）。
# 生成 40 层类初始化互相引用 Java 源（每层 check-cast/instance-of/宽值字段/
# 静态引用 + move-result 混合 + StringFactory + move-exception），javac+d8
# 产 tests/out/clinit_stress.dex，ASAN/UBSAN 编译 tests/core/clinit_stress_smoke.cc
# 执行全阶段。独立脚本（不动既有 tools/*.sh）。
# 用法: tools/clinit_stress_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/dexvm_clinit_stress"
GEN="$OUT/gen-src"
OUT_DEX_DIR="$CPP/tests/out"
DEX="$OUT_DEX_DIR/clinit_stress.dex"

JAVAC="/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/javac"
JAVA_HOME_DIR="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
D8="/Users/cao/Library/Android/sdk/build-tools/36.0.0/d8"
ANDROID_JAR="/Users/cao/Library/Android/sdk/platforms/android-35/android.jar"

if [[ ! -x "$JAVAC" || ! -x "$D8" ]]; then
  echo "clinit_stress_tests: javac/d8 not found" >&2
  exit 1
fi

LEVELS=40

rm -rf "$GEN"
mkdir -p "$GEN/stress" "$OUT_DEX_DIR"

echo "==== generate stress java ($LEVELS levels) ===="
{
  echo "package stress;"
  echo
  echo "public class Entry {"
  echo "  public static int run() {"
  echo "    C0.touch();"
  echo "    return 1;"
  echo "  }"
  echo
  echo "  public static int sum() {"
  echo "    int a = 0;"
  for ((i = 0; i < LEVELS; i++)); do echo "    a += C$i.check();"; done
  echo "    return a;"
  echo "  }"
  echo
  echo "  public static boolean verify() {"
  echo "    boolean ok = true;"
  for ((i = 0; i < LEVELS; i++)); do echo "    ok = ok && C$i.ok();"; done
  echo "    return ok;"
  echo "  }"
  echo
  echo "  public static long wideMix() {"
  echo "    long a = C0.mix(123456789L, 3);"
  for ((i = 1; i < LEVELS; i++)); do
    if ((i % 2 == 0)); then echo "    a ^= C$i.mix(a, $i);"; else echo "    a += C$i.mix(a, $i);"; fi
  done
  echo "    return a;"
  echo "  }"
  echo
  echo "  public static int scan(Object[] pool) {"
  echo "    int hits = 0;"
  echo "    for (int i = 0; i < pool.length; i++) {"
  echo "      Object x = pool[i];"
  echo "      if (x instanceof String) {"
  echo "        String t = (String) x;"
  echo "        hits += t.length();"
  echo "      } else if (x instanceof long[]) {"
  echo "        long[] arr = (long[]) x;"
  echo "        hits += (int) arr.length;"
  echo "      }"
  echo "      Object y = C0.d(x);"
  echo "      if (y != null) {"
  echo "        hits += 1;"
  echo "      }"
  echo "    }"
  echo "    return hits;"
  echo "  }"
  echo
  echo "  public static int alias() {"
  echo "    char[] c = new char[]{'a', 'b', 'c'};"
  echo "    String s = new String(c);"
  echo "    String t = new String(c);"
  echo "    return s.length() + t.length();"
  echo "  }"
  echo "}"
  echo
  for ((i = 0; i < LEVELS; i++)); do
    j=$((i + 1))
    if ((i == LEVELS - 1)); then j=$i; fi
    cat <<EOF
class C$i {
  static int n;
  static long w;
  static double dd;
  static Object o;
  static String s;
  static C$j typed;
  static C$i self;
  static long salt = 0x1122334455667788L + ${i}L;

  static void touch() {
    n += 1;
  }

  static int id() {
    return $i;
  }

  static long mix(long a, int b) {
    long r = a * 31L + b;
    r ^= (r >>> 17);
    return r;
  }

  static Object box(int v) {
    if ((v & 1) == 0) {
      return new String(new char[]{'s', (char) ('0' + (v % 10))});
    }
    return new long[]{v, v * 2L};
  }

  static C$i make() {
    return new C$i();
  }

  static Object d(Object x) {
    Object y = x;
    if (y instanceof String) {
      String t = (String) y;
      return t;
    }
    if (y instanceof long[]) {
      long[] a = (long[]) y;
      return new String(new char[]{(char) ('0' + a.length)});
    }
    return y;
  }

  static {
    C$j.touch();
    w = mix(0x0123456789abcdefL, id());
    dd = (double) w * 0.5d;
    try {
      o = box(n);
      Object tmp = d(o);
      if (tmp instanceof String) {
        n += 10;
      }
    } catch (RuntimeException e) {
      o = new String(new char[]{'x'});
    }
    s = new String(new char[]{'c', (char) ('0' + (id() % 10))});
    typed = C$j.make();
    self = make();
    w += mix(w, n);
    w ^= C$j.salt;
  }

  static int check() {
    if (self == null || s == null || o == null || typed == null) {
      return -1;
    }
    if (s.length() != 2) {
      return -1;
    }
    Object tmp = d(o);
    if (tmp == null) {
      return -1;
    }
    int acc = id() * 3 + 7;
    if (tmp instanceof String) {
      acc += 1;
    }
    return acc;
  }

  static boolean ok() {
    return self != null && typed != null && s != null && o != null;
  }
}

EOF
  done
} > "$GEN/stress/Entry.java"

echo "==== javac + d8 ===="
CLASSES_DIR="$OUT/classes"
D8_DIR="$OUT/d8-tmp"
rm -rf "$CLASSES_DIR" "$D8_DIR"
mkdir -p "$CLASSES_DIR" "$D8_DIR"
"$JAVAC" --release 8 -encoding UTF-8 -g -d "$CLASSES_DIR" "$GEN/stress/Entry.java"
CLASS_FILES=()
while IFS= read -r f; do
  CLASS_FILES[${#CLASS_FILES[@]}]="$f"
done <<EOF
$(find "$CLASSES_DIR" -name '*.class' | sort)
EOF
JAVA_HOME="$JAVA_HOME_DIR" \
  "$D8" --release --min-api 26 --lib "$ANDROID_JAR" --output "$D8_DIR" "${CLASS_FILES[@]}"
mv "$D8_DIR/classes.dex" "$DEX"
echo "wrote $DEX ($(wc -c < "$DEX" | tr -d ' ') bytes)"

echo "==== compile clinit_stress_smoke (ASAN/UBSAN) ===="
mkdir -p "$OUT/bin"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/clinit_stress_smoke.cc" \
  -o "$OUT/bin/clinit_stress_smoke"

echo "==== run clinit_stress_smoke ===="
exec "$OUT/bin/clinit_stress_smoke" "$DEX"
