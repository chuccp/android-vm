package android.os;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;

/**
 * 可持久化包（AOSP android.os.PersistableBundle 公开面；§31.7 C 档 lib POJO——
 * 进程内键值表）。
 */
public final class PersistableBundle {

    private final Map<String, Object> mMap = new HashMap<String, Object>();

    public PersistableBundle() {
    }

    public PersistableBundle(PersistableBundle b) {
        if (b != null) this.mMap.putAll(b.mMap);
    }

    public static PersistableBundle forPair(String key, String value) {
        PersistableBundle b = new PersistableBundle();
        b.putString(key, value);
        return b;
    }

    public void putString(String key, String value) {
        this.mMap.put(key, value);
    }

    public void putInt(String key, int value) {
        this.mMap.put(key, Integer.valueOf(value));
    }

    public void putLong(String key, long value) {
        this.mMap.put(key, Long.valueOf(value));
    }

    public void putDouble(String key, double value) {
        this.mMap.put(key, Double.valueOf(value));
    }

    public void putBoolean(String key, boolean value) {
        this.mMap.put(key, Boolean.valueOf(value));
    }

    public void putPersistableBundle(String key, PersistableBundle value) {
        this.mMap.put(key, value);
    }

    public void putStringArray(String key, String[] value) {
        this.mMap.put(key, value);
    }

    public void putIntArray(String key, int[] value) {
        this.mMap.put(key, value);
    }

    public void putLongArray(String key, long[] value) {
        this.mMap.put(key, value);
    }

    public void putDoubleArray(String key, double[] value) {
        this.mMap.put(key, value);
    }

    public void putBooleanArray(String key, boolean[] value) {
        this.mMap.put(key, value);
    }

    public String getString(String key) {
        return getString(key, null);
    }

    public String getString(String key, String defaultValue) {
        Object v = this.mMap.get(key);
        return v instanceof String ? (String) v : defaultValue;
    }

    public int getInt(String key) {
        return getInt(key, 0);
    }

    public int getInt(String key, int defaultValue) {
        Object v = this.mMap.get(key);
        return v instanceof Integer ? (Integer) v : defaultValue;
    }

    public long getLong(String key) {
        return getLong(key, 0);
    }

    public long getLong(String key, long defaultValue) {
        Object v = this.mMap.get(key);
        return v instanceof Long ? (Long) v : defaultValue;
    }

    public double getDouble(String key) {
        return getDouble(key, 0);
    }

    public double getDouble(String key, double defaultValue) {
        Object v = this.mMap.get(key);
        return v instanceof Double ? (Double) v : defaultValue;
    }

    public boolean getBoolean(String key) {
        return getBoolean(key, false);
    }

    public boolean getBoolean(String key, boolean defaultValue) {
        Object v = this.mMap.get(key);
        return v instanceof Boolean ? (Boolean) v : defaultValue;
    }

    public PersistableBundle getPersistableBundle(String key) {
        Object v = this.mMap.get(key);
        return v instanceof PersistableBundle ? (PersistableBundle) v : null;
    }

    public String[] getStringArray(String key) {
        Object v = this.mMap.get(key);
        return v instanceof String[] ? (String[]) v : null;
    }

    public int[] getIntArray(String key) {
        Object v = this.mMap.get(key);
        return v instanceof int[] ? (int[]) v : null;
    }

    public long[] getLongArray(String key) {
        Object v = this.mMap.get(key);
        return v instanceof long[] ? (long[]) v : null;
    }

    public double[] getDoubleArray(String key) {
        Object v = this.mMap.get(key);
        return v instanceof double[] ? (double[]) v : null;
    }

    public boolean[] getBooleanArray(String key) {
        Object v = this.mMap.get(key);
        return v instanceof boolean[] ? (boolean[]) v : null;
    }

    public Object get(String key) {
        return this.mMap.get(key);
    }

    public void remove(String key) {
        this.mMap.remove(key);
    }

    public void clear() {
        this.mMap.clear();
    }

    public boolean containsKey(String key) {
        return this.mMap.containsKey(key);
    }

    public int size() {
        return this.mMap.size();
    }

    public String[] keySet() {
        Set<String> keys = this.mMap.keySet();
        return keys.toArray(new String[keys.size()]);
    }

    public void persistToFile(java.io.File f) {
    }

    public static PersistableBundle restoreFromFile(java.io.File f) {
        return new PersistableBundle();
    }

    @Override
    public String toString() {
        return "PersistableBundle[" + this.mMap.size() + " keys]";
    }
}
