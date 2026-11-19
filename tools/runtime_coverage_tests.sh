#!/usr/bin/env bash
# runtime.dex 类覆盖自检（单一事实源口径，废除 allowlist/excluded 表单后）：
#   1. 「内建注册集 == 派生集」一致性断言（防再犯核心）：编译并运行
#      tests/core/builtin_manifest_smoke.cc —— RegisterBuiltinClass 实际登记集
#      与 core/builtin_classes.def（派生集，构建打包排除集同源）双向比对。
#   2. 解析 dex class_defs 断言关键类在 runtime.dex 包内（ServiceLoader/LinkedList/
#      CopyOnWriteArrayList/ThreadLocal 等）+ 抽样 + 排除集零泄漏（builtin_classes.def
#      派生排除集内类不得被定义；前缀规则 name / name$…）。
# 用法: tools/runtime_coverage_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPP="$ROOT/app/src/main/cpp"
DEX="$ROOT/runtime-libs/build/runtime-dex/runtime.dex"
MANIFEST="$CPP/core/builtin_classes.def"
OUT="${TMPDIR:-/tmp}/dexvm_runtime_coverage"

if [ ! -f "$DEX" ]; then
  echo "runtime_coverage_tests: missing $DEX (run :runtime-libs:build first)" >&2
  exit 1
fi
if [ ! -f "$MANIFEST" ]; then
  echo "runtime_coverage_tests: missing $MANIFEST (single source of builtin class set)" >&2
  exit 1
fi

# ---- 1. 内建注册集 == 派生集（builtin_classes.def）----
echo "==== builtin manifest consistency (registered == derived) ===="
mkdir -p "$OUT"
xcrun clang++ -std=c++20 -fno-exceptions -fno-rtti -g -O1 \
  -I "$CPP" \
  "$CPP"/core/*.cc \
  "$CPP/memory/heap.cc" \
  "$CPP/session/session.cc" \
  "$CPP/session/class_loader.cc" \
  "$CPP/framework/lifecycle_driver.cc" \
  "$CPP"/dex/*.cc \
  "$CPP/tests/core/builtin_manifest_smoke.cc" \
  -lz \
  -o "$OUT/builtin_manifest_smoke"
"$OUT/builtin_manifest_smoke"

# ---- 2. dex 覆盖 + 排除集零泄漏（派生集读 builtin_classes.def）----
python3 -I - "$DEX" "$MANIFEST" <<'PYEOF'
import re, struct, sys

dex_path, manifest_path = sys.argv[1], sys.argv[2]
data = open(dex_path, 'rb').read()

def u4(off):
    return struct.unpack_from('<I', data, off)[0]

string_ids_size, string_ids_off = u4(56), u4(60)
type_ids_size, type_ids_off = u4(64), u4(68)
class_defs_size, class_defs_off = u4(96), u4(100)

def string(idx):
    off = u4(string_ids_off + 4 * idx)
    # uleb128 utf16_size then mutf8
    shift = 0
    while data[off] & 0x80:
        off += 1
    off += 1
    end = data.index(b'\x00', off)
    return data[off:end].decode('utf-8', 'replace')

def type_desc(idx):
    return string(u4(type_ids_off + 4 * idx))

defined = set()
for i in range(class_defs_size):
    class_idx = u4(class_defs_off + 32 * i)
    defined.add(type_desc(class_idx))

failures = []

must = [
    'Ljava/util/ServiceLoader;',
    'Ljava/util/LinkedList;',
    'Ljava/util/concurrent/CopyOnWriteArrayList;',
    'Ljava/lang/ThreadLocal;',
    'Ljava/util/Optional;',
    'Ljava/util/stream/Stream;',
    'Ljava/util/stream/Collectors;',
    'Ljava/lang/CharSequence;',
    'Ljava/lang/Comparable;',
    'Ljava/lang/Iterable;',
    'Ljava/io/Serializable;',
    'Ljava/util/NoSuchElementException;',
    'Ljava/util/concurrent/atomic/AtomicReference;',
    'Ljava/util/concurrent/atomic/AtomicLong;',
    'Ljava/util/concurrent/atomic/AtomicBoolean;',
    'Ljava/net/URL;',
    'Ljava/util/function/Consumer;',
    'Ljava/util/function/IntConsumer;',
    'Ljava/time/LocalDate;',
    'Ljava/time/LocalDateTime;',
    'Ljava/math/BigInteger;',
    'Ljava/security/MessageDigest;',
    'Ljava/util/regex/Pattern;',
    'Ljava/util/jar/JarFile;',
    'Lsun/security/action/GetPropertyAction;',
    'Lsun/security/util/SecurityConstants;',
    'Ljdk/internal/misc/SharedSecrets;',
    'Ljdk/internal/misc/JavaLangAccess;',
    'Lsun/util/locale/BaseLocale;',
]
for cls in must:
    if cls not in defined:
        failures.append('MISSING ' + cls)

samples = [
    'Ljava/util/ImmutableCollections$ListN;',
    'Ljava/util/ArrayDeque;',
    'Ljava/util/concurrent/CompletableFuture;',
    'Ljava/util/stream/ReferencePipeline;',
    'Ljava/nio/file/GuestFileSystem;',
    'Ljava/net/InetAddress;',
    'Ljava/security/cert/X509Certificate;',
    'Ljavax/crypto/Cipher;',
    'Ljavax/net/ssl/SSLContext;',
    'Ljava/time/format/DateTimeFormatter;',
    'Ljava/time/chrono/IsoChronology;',
    'Ljava/text/SimpleDateFormat;',
]
for cls in samples:
    if cls not in defined:
        failures.append('MISSING sample ' + cls)

# 派生排除集（单一事实源 builtin_classes.def）零泄漏（前缀规则 name / name$…）
excluded = []
for line in open(manifest_path):
    m = re.match(r'^DEXVM_(?:CORE|FRAMEWORK)_BUILTIN_CLASS\("([^"]+)"\)', line.strip())
    if m:
        desc = m.group(1)
        assert desc.startswith('L') and desc.endswith(';'), desc
        excluded.append(desc[1:-1].replace('/', '.'))
if len(excluded) != len(set(excluded)):
    failures.append('duplicate entries in builtin_classes.def')
if not excluded:
    failures.append('empty derived exclusion set from builtin_classes.def')

leaked = []
for cls in defined:
    for name in excluded:
        prefix = 'L' + name.replace('.', '/')
        if cls == prefix + ';' or cls.startswith(prefix + '$'):
            leaked.append(cls)
            break

if leaked:
    failures.append('LEAKED derived-exclusion classes: ' + ', '.join(sorted(set(leaked))))

if failures:
    print('runtime_coverage_tests: FAIL')
    for f in failures:
        print('  ' + f)
    sys.exit(1)
print('runtime_coverage_tests: PASS (defined=%d; must=%d; samples=%d; derived-excluded=%d clean)'
      % (len(defined), len(must), len(samples), len(excluded)))
PYEOF
