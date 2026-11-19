package android.content;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * SharedPreferences 内存实现（§31.7 B 档：lib 路径；AOSP
 * android.app.SharedPreferencesImpl 同名隐藏类的 VM 语义收敛面）。
 *
 * <p>形态：per-name 静态单例（{@link #getInstance}）+ 内存 HashMap 键值对；
 * xml 落盘后置（VM 侧内存 Map 形态无落盘面，apply/commit 均内存生效）。
 * 语义要点（AOSP 公开面）：putString(key, null) 等价 remove；clear 先清库、
 * 后序 modified 生效（同键后写覆盖）；类型不符按泛型擦除运行时语义抛
 * ClassCastException；getStringSet/getStringSet 均取/存快照副本。
 * listener 通知：commit 同步、apply 后置（下次 commit/apply 前落下）。
 */
public final class SharedPreferencesImpl implements SharedPreferences {
    private static final HashMap<String, SharedPreferencesImpl> sInstances =
            new HashMap<>();

    /** 同名单例入口（VM Context.getSharedPreferences 薄钩子转调点）。 */
    public static SharedPreferences getInstance(String name) {
        String key = (name == null) ? "" : name;
        synchronized (sInstances) {
            SharedPreferencesImpl inst = sInstances.get(key);
            if (inst == null) {
                inst = new SharedPreferencesImpl(key);
                sInstances.put(key, inst);
            }
            return inst;
        }
    }

    private final String mName;
    private final HashMap<String, Object> mMap = new HashMap<>();
    private final ArrayList<SharedPreferences$OnSharedPreferenceChangeListener> mListeners =
            new ArrayList<>();
    /** apply 后置回调键队列（去重保序）。 */
    private final ArrayList<String> mPendingNotify = new ArrayList<>();

    private SharedPreferencesImpl(String name) {
        mName = name;
        load();
    }

    /** 从 <guestDataRoot>/shared_prefs/<name>.xml 装载（ASCII 线格式；容错）。 */
    private void load() {
        String raw;
        try {
            raw = VmPrefsStore.read(mName);
        } catch (Throwable t) {
            return;
        }
        if (raw == null || raw.isEmpty()) {
            try {
                android.util.Log.i("DexVmPrefs", "open '" + mName + "' (empty)");
            } catch (Throwable ignored) {
            }
            return;
        }
        try {
            android.util.Log.i("DexVmPrefs", "open '" + mName + "' bytes=" + raw.length());
        } catch (Throwable ignored) {
        }
        synchronized (this) {
            String[] lines = raw.split("\n");
            for (int i = 0; i < lines.length; i++) {
                String line = lines[i];
                if (line.isEmpty() || "v1".equals(line)) continue;
                String[] f = line.split("\t", -1);
                if (f.length < 3) continue;
                try {
                    String key = decode(f[1]);
                    if ("s".equals(f[0])) {
                        mMap.put(key, decode(f[2]));
                    } else if ("i".equals(f[0])) {
                        mMap.put(key, Integer.valueOf(f[2]));
                    } else if ("l".equals(f[0])) {
                        mMap.put(key, Long.valueOf(f[2]));
                    } else if ("f".equals(f[0])) {
                        mMap.put(key, Float.valueOf(f[2]));
                    } else if ("b".equals(f[0])) {
                        mMap.put(key, Boolean.valueOf("1".equals(f[2])));
                    } else if ("S".equals(f[0])) {
                        Set<String> set = new HashSet<String>();
                        if (!f[2].isEmpty()) {
                            String[] items = f[2].split(",");
                            for (int j = 0; j < items.length; j++) set.add(decode(items[j]));
                        }
                        mMap.put(key, set);
                    }
                } catch (Throwable ignored) {
                }
            }
        }
    }

    /** 全量落盘（快照后序列化；失败静默——内存语义仍生效）。 */
    private void persist() {
        HashMap<String, Object> snapshot;
        synchronized (this) {
            snapshot = new HashMap<String, Object>(mMap);
        }
        StringBuilder sb = new StringBuilder("v1\n");
        for (Map.Entry<String, Object> e : snapshot.entrySet()) {
            String key = encode(e.getKey());
            Object v = e.getValue();
            if (v instanceof String) {
                sb.append("s\t").append(key).append('\t').append(encode((String) v));
            } else if (v instanceof Integer) {
                sb.append("i\t").append(key).append('\t').append(v);
            } else if (v instanceof Long) {
                sb.append("l\t").append(key).append('\t').append(v);
            } else if (v instanceof Float) {
                sb.append("f\t").append(key).append('\t').append(v);
            } else if (v instanceof Boolean) {
                sb.append("b\t").append(key).append('\t')
                        .append(((Boolean) v).booleanValue() ? '1' : '0');
            } else if (v instanceof Set) {
                sb.append("S\t").append(key).append('\t');
                boolean first = true;
                for (Object item : (Set<?>) v) {
                    if (!first) sb.append(',');
                    first = false;
                    sb.append(encode(String.valueOf(item)));
                }
            } else {
                continue;
            }
            sb.append('\n');
        }
        try {
            android.util.Log.i("DexVmPrefs", "persist '" + mName + "' keys="
                    + snapshot.size() + " bytes=" + sb.length());
        } catch (Throwable ignored) {
        }
        try {
            VmPrefsStore.write(mName, sb.toString());
        } catch (Throwable ignored) {
        }
    }

    private static final char[] HEX = "0123456789abcdef".toCharArray();

    /** 十六进制编码（ASCII 安全；不依赖 Base64 的内部 String(byte[],int,int,int)）。 */
    private static String encode(String s) {
        byte[] b = s.getBytes(StandardCharsets.UTF_8);
        StringBuilder sb = new StringBuilder(b.length * 2);
        for (int i = 0; i < b.length; i++) {
            int v = b[i] & 0xff;
            sb.append(HEX[v >>> 4]).append(HEX[v & 0x0f]);
        }
        return sb.toString();
    }

    private static String decode(String s) {
        int n = s.length() / 2;
        byte[] b = new byte[n];
        for (int i = 0; i < n; i++) {
            int hi = hexDigit(s.charAt(2 * i));
            int lo = hexDigit(s.charAt(2 * i + 1));
            b[i] = (byte) ((hi << 4) | lo);
        }
        return new String(b, StandardCharsets.UTF_8);
    }

    private static int hexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    }

    @Override
    public Map<String, ?> getAll() {
        synchronized (this) {
            return new HashMap<String, Object>(mMap);
        }
    }

    @Override
    public String getString(String key, String defValue) {
        synchronized (this) {
            Object v = mMap.get(key);
            return (v == null) ? defValue : (String) v;
        }
    }

    @Override
    @SuppressWarnings("unchecked")
    public Set<String> getStringSet(String key, Set<String> defValues) {
        synchronized (this) {
            Object v = mMap.get(key);
            return (v == null) ? defValues : new HashSet<String>((Set<String>) v);
        }
    }

    @Override
    public int getInt(String key, int defValue) {
        synchronized (this) {
            Object v = mMap.get(key);
            return (v == null) ? defValue : ((Integer) v).intValue();
        }
    }

    @Override
    public long getLong(String key, long defValue) {
        synchronized (this) {
            Object v = mMap.get(key);
            return (v == null) ? defValue : ((Long) v).longValue();
        }
    }

    @Override
    public float getFloat(String key, float defValue) {
        synchronized (this) {
            Object v = mMap.get(key);
            return (v == null) ? defValue : ((Float) v).floatValue();
        }
    }

    @Override
    public boolean getBoolean(String key, boolean defValue) {
        synchronized (this) {
            Object v = mMap.get(key);
            return (v == null) ? defValue : ((Boolean) v).booleanValue();
        }
    }

    @Override
    public boolean contains(String key) {
        synchronized (this) {
            boolean r = mMap.containsKey(key);
            try {
                // TEMP DIAG: 隐私态探测键名/结果（CloudIDRequestProxy h() 链；事后移除）
                android.util.Log.i("DexVmSp", "contains '" + mName + "' key='" + key + "' -> " + r);
            } catch (Throwable ignored) {
            }
            return r;
        }
    }

    @Override
    public SharedPreferences$Editor edit() {
        return new EditorImpl();
    }

    @Override
    public void registerOnSharedPreferenceChangeListener(
            SharedPreferences$OnSharedPreferenceChangeListener listener) {
        synchronized (this) {
            if (listener != null && !mListeners.contains(listener)) {
                mListeners.add(listener);
            }
        }
    }

    @Override
    public void unregisterOnSharedPreferenceChangeListener(
            SharedPreferences$OnSharedPreferenceChangeListener listener) {
        synchronized (this) {
            mListeners.remove(listener);
        }
    }

    /** listener 通知（变更键序；回调面异常不扩散——骨架原则可执行不崩）。 */
    private void notifyListeners(ArrayList<String> keys) {
        if (keys.isEmpty()) return;
        ArrayList<SharedPreferences$OnSharedPreferenceChangeListener> ls;
        synchronized (this) {
            ls = new ArrayList<>(mListeners);
        }
        for (int i = 0; i < keys.size(); i++) {
            String key = keys.get(i);
            for (int j = 0; j < ls.size(); j++) {
                try {
                    ls.get(j).onSharedPreferenceChanged(this, key);
                } catch (RuntimeException ignored) {
                    // 回调异常不阻断后续通知（考试链优先连续性）
                }
            }
        }
    }

    /** apply 后置回调落点（下次 commit/apply 前）。 */
    private void flushPendingNotifications() {
        ArrayList<String> keys;
        synchronized (this) {
            if (mPendingNotify.isEmpty()) return;
            keys = new ArrayList<>(mPendingNotify);
            mPendingNotify.clear();
        }
        notifyListeners(keys);
    }

    /** 编辑器（AOSP EditorImpl.mModified 同语义）。 */
    private final class EditorImpl implements SharedPreferences$Editor {
        private final HashMap<String, Object> mModified = new HashMap<>();
        private final ArrayList<String> mOrder = new ArrayList<>();
        private boolean mClear = false;

        private SharedPreferences$Editor record(String key, Object value) {
            synchronized (SharedPreferencesImpl.this) {
                mModified.put(key, value);  // null = remove（AOSP 语义）
                if (!mOrder.contains(key)) {
                    mOrder.add(key);
                }
                return this;
            }
        }

        @Override
        public SharedPreferences$Editor putString(String key, String value) {
            return record(key, value);
        }

        @Override
        public SharedPreferences$Editor putStringSet(String key, Set<String> values) {
            return record(key, (values == null) ? null : new HashSet<>(values));
        }

        @Override
        public SharedPreferences$Editor putInt(String key, int value) {
            return record(key, Integer.valueOf(value));
        }

        @Override
        public SharedPreferences$Editor putLong(String key, long value) {
            return record(key, Long.valueOf(value));
        }

        @Override
        public SharedPreferences$Editor putFloat(String key, float value) {
            return record(key, Float.valueOf(value));
        }

        @Override
        public SharedPreferences$Editor putBoolean(String key, boolean value) {
            return record(key, Boolean.valueOf(value));
        }

        @Override
        public SharedPreferences$Editor remove(String key) {
            return record(key, null);
        }

        @Override
        public SharedPreferences$Editor clear() {
            synchronized (SharedPreferencesImpl.this) {
                mClear = true;
                return this;
            }
        }

        /** 待提交 -> 内存 Map；返回受影响键（去重保序，通知序）。 */
        private ArrayList<String> commitToMemory() {
            ArrayList<String> changed = new ArrayList<>();
            synchronized (SharedPreferencesImpl.this) {
                if (mClear) {
                    for (String k : mMap.keySet()) {
                        if (!changed.contains(k)) {
                            changed.add(k);
                        }
                    }
                    mMap.clear();
                    mClear = false;
                }
                for (int i = 0; i < mOrder.size(); i++) {
                    String k = mOrder.get(i);
                    Object v = mModified.get(k);
                    if (v == null) {
                        if (mMap.containsKey(k)) {
                            mMap.remove(k);
                            if (!changed.contains(k)) {
                                changed.add(k);
                            }
                        }
                    } else {
                        mMap.put(k, v);
                        if (!changed.contains(k)) {
                            changed.add(k);
                        }
                    }
                }
                mModified.clear();
                mOrder.clear();
            }
            return changed;
        }

        @Override
        public boolean commit() {
            flushPendingNotifications();  // 先落之前 apply 的后置回调
            ArrayList<String> changed = commitToMemory();
            persist();
            notifyListeners(changed);
            return true;
        }

        @Override
        public void apply() {
            flushPendingNotifications();
            ArrayList<String> changed = commitToMemory();
            persist();
            if (!changed.isEmpty()) {
                synchronized (SharedPreferencesImpl.this) {
                    for (String k : changed) {
                        if (!mPendingNotify.contains(k)) {
                            mPendingNotify.add(k);
                        }
                    }
                }
            }
        }
    }
}
