package android.os;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

/**
 * 键值数据载体（Android 公开面；§31.7 B 档 POJO→lib 形态）。
 *
 * <p>AOSP Bundle/BaseBundle 的 VM 语义收敛面：内存 HashMap 存储 + 类型化
 * put/get 家族。Parcelable/PersistableBundle 面（Parcel 读写）后置——
 * 链上暴露时按同法逐轮补。类型不符按泛型擦除运行时语义抛
 * ClassCastException；getString 等对 null 值返回 null（AOSP 语义）。
 */
public class Bundle implements Cloneable {
    private final HashMap<String, Object> mMap;
    // AOSP BaseBundle.mClassLoader：Parcel 反序列化用的 ClassLoader 槽。
    // 本 VM 的 Bundle 存内存对象（无 Parcel 解包），仅保留 get/set 语义面
    // ——androidx Fragment.instantiate 等在构造 arguments 后设置它。
    private ClassLoader mClassLoader;

    public Bundle() {
        mMap = new HashMap<>();
    }

    public Bundle(int capacity) {
        mMap = new HashMap<>(capacity > 0 ? capacity : 1);
    }

    public Bundle(Bundle from) {
        mMap = (from == null) ? new HashMap<String, Object>() : new HashMap<>(from.mMap);
    }

    public Bundle(Map<String, Object> from) {
        mMap = (from == null) ? new HashMap<String, Object>() : new HashMap<>(from);
    }

    // ---- ClassLoader 面（AOSP BaseBundle）----

    public void setClassLoader(ClassLoader loader) {
        mClassLoader = loader;
    }

    public ClassLoader getClassLoader() {
        return mClassLoader;
    }

    // ---- 容量面 ----

    public int size() {
        return mMap.size();
    }

    public boolean isEmpty() {
        return mMap.isEmpty();
    }

    public void clear() {
        mMap.clear();
    }

    public boolean containsKey(String key) {
        return mMap.containsKey(key);
    }

    /** AOSP BaseBundle.get(String)：裸 Object 存取面（androidx Fragment 等使用）。 */
    public Object get(String key) {
        return mMap.get(key);
    }

    /** AOSP 签名：android.os.Bundle.remove(String) 返回 void（非 Map 语义）。 */
    public void remove(String key) {
        mMap.remove(key);
    }

    public Set<String> keySet() {
        return mMap.keySet();
    }

    public void putAll(Bundle bundle) {
        if (bundle != null) {
            mMap.putAll(bundle.mMap);
        }
    }

    // ---- String / CharSequence ----

    public void putString(String key, String value) {
        mMap.put(key, value);
    }

    public String getString(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (String) o;
    }

    public String getString(String key, String defaultValue) {
        String s = getString(key);
        return (s != null) ? s : defaultValue;
    }

    public void putCharSequence(String key, CharSequence value) {
        mMap.put(key, value);
    }

    public CharSequence getCharSequence(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (CharSequence) o;
    }

    public CharSequence getCharSequence(String key, CharSequence defaultValue) {
        CharSequence s = getCharSequence(key);
        return (s != null) ? s : defaultValue;
    }

    // ---- 基本型 ----

    public void putInt(String key, int value) {
        mMap.put(key, Integer.valueOf(value));
    }

    public int getInt(String key) {
        return getInt(key, 0);
    }

    public int getInt(String key, int defaultValue) {
        Object o = mMap.get(key);
        return (o == null) ? defaultValue : ((Integer) o).intValue();
    }

    public void putLong(String key, long value) {
        mMap.put(key, Long.valueOf(value));
    }

    public long getLong(String key) {
        return getLong(key, 0L);
    }

    public long getLong(String key, long defaultValue) {
        Object o = mMap.get(key);
        return (o == null) ? defaultValue : ((Long) o).longValue();
    }

    public void putShort(String key, short value) {
        mMap.put(key, Short.valueOf(value));
    }

    public short getShort(String key) {
        return getShort(key, (short) 0);
    }

    public short getShort(String key, short defaultValue) {
        Object o = mMap.get(key);
        return (o == null) ? defaultValue : ((Short) o).shortValue();
    }

    public void putFloat(String key, float value) {
        mMap.put(key, Float.valueOf(value));
    }

    public float getFloat(String key) {
        return getFloat(key, 0.0f);
    }

    public float getFloat(String key, float defaultValue) {
        Object o = mMap.get(key);
        return (o == null) ? defaultValue : ((Float) o).floatValue();
    }

    public void putDouble(String key, double value) {
        mMap.put(key, Double.valueOf(value));
    }

    public double getDouble(String key) {
        return getDouble(key, 0.0);
    }

    public double getDouble(String key, double defaultValue) {
        Object o = mMap.get(key);
        return (o == null) ? defaultValue : ((Double) o).doubleValue();
    }

    public void putChar(String key, char value) {
        mMap.put(key, Character.valueOf(value));
    }

    public char getChar(String key) {
        return getChar(key, (char) 0);
    }

    public char getChar(String key, char defaultValue) {
        Object o = mMap.get(key);
        return (o == null) ? defaultValue : ((Character) o).charValue();
    }

    public void putByte(String key, byte value) {
        mMap.put(key, Byte.valueOf(value));
    }

    public byte getByte(String key) {
        return getByte(key, (byte) 0);
    }

    public byte getByte(String key, byte defaultValue) {
        Object o = mMap.get(key);
        return (o == null) ? defaultValue : ((Byte) o).byteValue();
    }

    public void putBoolean(String key, boolean value) {
        mMap.put(key, Boolean.valueOf(value));
    }

    public boolean getBoolean(String key) {
        return getBoolean(key, false);
    }

    public boolean getBoolean(String key, boolean defaultValue) {
        Object o = mMap.get(key);
        return (o == null) ? defaultValue : ((Boolean) o).booleanValue();
    }

    // ---- 数组面 ----

    public void putStringArray(String key, String[] value) {
        mMap.put(key, value);
    }

    public String[] getStringArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (String[]) o;
    }

    public void putCharSequenceArray(String key, CharSequence[] value) {
        mMap.put(key, value);
    }

    public CharSequence[] getCharSequenceArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (CharSequence[]) o;
    }

    public void putIntArray(String key, int[] value) {
        mMap.put(key, value);
    }

    public int[] getIntArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (int[]) o;
    }

    public void putLongArray(String key, long[] value) {
        mMap.put(key, value);
    }

    public long[] getLongArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (long[]) o;
    }

    public void putShortArray(String key, short[] value) {
        mMap.put(key, value);
    }

    public short[] getShortArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (short[]) o;
    }

    public void putFloatArray(String key, float[] value) {
        mMap.put(key, value);
    }

    public float[] getFloatArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (float[]) o;
    }

    public void putDoubleArray(String key, double[] value) {
        mMap.put(key, value);
    }

    public double[] getDoubleArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (double[]) o;
    }

    public void putCharArray(String key, char[] value) {
        mMap.put(key, value);
    }

    public char[] getCharArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (char[]) o;
    }

    public void putByteArray(String key, byte[] value) {
        mMap.put(key, value);
    }

    public byte[] getByteArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (byte[]) o;
    }

    public void putBooleanArray(String key, boolean[] value) {
        mMap.put(key, value);
    }

    public boolean[] getBooleanArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (boolean[]) o;
    }

    // ---- ArrayList 面 ----

    public void putIntegerArrayList(String key, ArrayList<Integer> value) {
        mMap.put(key, value);
    }

    @SuppressWarnings("unchecked")
    public ArrayList<Integer> getIntegerArrayList(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (ArrayList<Integer>) o;
    }

    public void putStringArrayList(String key, ArrayList<String> value) {
        mMap.put(key, value);
    }

    @SuppressWarnings("unchecked")
    public ArrayList<String> getStringArrayList(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (ArrayList<String>) o;
    }

    public void putCharSequenceArrayList(String key, ArrayList<CharSequence> value) {
        mMap.put(key, value);
    }

    @SuppressWarnings("unchecked")
    public ArrayList<CharSequence> getCharSequenceArrayList(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (ArrayList<CharSequence>) o;
    }

    // ---- Bundle 嵌套 ----

    public void putBundle(String key, Bundle value) {
        mMap.put(key, value);
    }

    public Bundle getBundle(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (Bundle) o;
    }

    // ---- Parcelable / Serializable 面（AOSP BaseBundle 公开签名）----

    public void putParcelable(String key, Parcelable value) {
        mMap.put(key, value);
    }

    public Parcelable getParcelable(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (Parcelable) o;
    }

    public void putParcelableArray(String key, Parcelable[] value) {
        mMap.put(key, value);
    }

    @SuppressWarnings("unchecked")
    public <T extends Parcelable> T[] getParcelableArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (T[]) o;
    }

    public void putParcelableArrayList(String key, ArrayList<? extends Parcelable> value) {
        mMap.put(key, value);
    }

    @SuppressWarnings("unchecked")
    public <T extends Parcelable> ArrayList<T> getParcelableArrayList(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (ArrayList<T>) o;
    }

    public <T extends Parcelable> void putSparseParcelableArray(
            String key, android.util.SparseArray<T> value) {
        mMap.put(key, value);
    }

    @SuppressWarnings("unchecked")
    public <T extends Parcelable> android.util.SparseArray<T> getSparseParcelableArray(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (android.util.SparseArray<T>) o;
    }

    public void putSerializable(String key, java.io.Serializable value) {
        mMap.put(key, value);
    }

    public java.io.Serializable getSerializable(String key) {
        Object o = mMap.get(key);
        return (o == null) ? null : (java.io.Serializable) o;
    }

    // ---- Object 面 ----

    @Override
    public Bundle clone() {
        return new Bundle(this);  // 深拷贝（Map 复制；AOSP clone 语义）
    }

    @Override
    public String toString() {
        return "Bundle" + mMap.toString();
    }
}
