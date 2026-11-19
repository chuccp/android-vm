package android.content;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;

/**
 * 键值数据载体（Android 公开面；§31.7 C 档 POJO→lib 形态——原 builtin 空壳
 * put/getAsString 骨架归位 lib 真语义）。
 *
 * <p>AOSP android.content.ContentValues 收敛面：内存 HashMap 存储 + 类型化
 * put/getAs* 家族。getAs* 类型不符按 AOSP 转换口径（Number 互转、
 * toString 解析）；null 值/缺键返回 null（AOSP 语义）。Parcelable 面
 * （Parcel 读写）后置——链上暴露时按同法补。
 */
public final class ContentValues {

    private final HashMap<String, Object> mValues;

    public ContentValues() {
        mValues = new HashMap<String, Object>();
    }

    public ContentValues(int size) {
        mValues = new HashMap<String, Object>(size > 0 ? size : 1);
    }

    public ContentValues(ContentValues from) {
        mValues = new HashMap<String, Object>(from == null
                ? new HashMap<String, Object>() : from.mValues);
    }

    public ContentValues(Map<String, Object> from) {
        mValues = new HashMap<String, Object>(from == null
                ? new HashMap<String, Object>() : from);
    }

    // ---- 容量面 ----

    public int size() {
        return mValues.size();
    }

    public void remove(String key) {
        mValues.remove(key);
    }

    public void clear() {
        mValues.clear();
    }

    public boolean containsKey(String key) {
        return mValues.containsKey(key);
    }

    public Object get(String key) {
        return mValues.get(key);
    }

    // ---- 类型化读取（AOSP 转换口径）----

    public String getAsString(String key) {
        Object value = mValues.get(key);
        return value != null ? value.toString() : null;
    }

    public Long getAsLong(String key) {
        Object value = mValues.get(key);
        if (value == null) {
            return null;
        }
        try {
            return (value instanceof Long) ? (Long) value : Long.valueOf(value.toString());
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public Integer getAsInteger(String key) {
        Object value = mValues.get(key);
        if (value == null) {
            return null;
        }
        try {
            return (value instanceof Integer) ? (Integer) value : Integer.valueOf(value.toString());
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public Short getAsShort(String key) {
        Object value = mValues.get(key);
        if (value == null) {
            return null;
        }
        try {
            return (value instanceof Short) ? (Short) value : Short.valueOf(value.toString());
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public Byte getAsByte(String key) {
        Object value = mValues.get(key);
        if (value == null) {
            return null;
        }
        try {
            return (value instanceof Byte) ? (Byte) value : Byte.valueOf(value.toString());
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public Double getAsDouble(String key) {
        Object value = mValues.get(key);
        if (value == null) {
            return null;
        }
        try {
            return (value instanceof Double) ? (Double) value : Double.valueOf(value.toString());
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public Float getAsFloat(String key) {
        Object value = mValues.get(key);
        if (value == null) {
            return null;
        }
        try {
            return (value instanceof Float) ? (Float) value : Float.valueOf(value.toString());
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public Boolean getAsBoolean(String key) {
        Object value = mValues.get(key);
        if (value == null) {
            return null;
        }
        return (value instanceof Boolean) ? (Boolean) value : Boolean.valueOf(value.toString());
    }

    public byte[] getAsByteArray(String key) {
        Object value = mValues.get(key);
        return (value instanceof byte[]) ? (byte[]) value : null;
    }

    // ---- 类型化写入 ----

    public void put(String key, String value) {
        mValues.put(key, value);
    }

    public void put(String key, Byte value) {
        mValues.put(key, value);
    }

    public void put(String key, Short value) {
        mValues.put(key, value);
    }

    public void put(String key, Integer value) {
        mValues.put(key, value);
    }

    public void put(String key, Long value) {
        mValues.put(key, value);
    }

    public void put(String key, Float value) {
        mValues.put(key, value);
    }

    public void put(String key, Double value) {
        mValues.put(key, value);
    }

    public void put(String key, Boolean value) {
        mValues.put(key, value);
    }

    public void put(String key, byte[] value) {
        mValues.put(key, value);
    }

    public void putNull(String key) {
        mValues.put(key, null);
    }

    public void putAll(ContentValues other) {
        if (other != null) {
            mValues.putAll(other.mValues);
        }
    }

    // ---- 键值集视图 ----

    public Set<String> keySet() {
        return mValues.keySet();
    }

    public Set<Map.Entry<String, Object>> valueSet() {
        return mValues.entrySet();
    }

    // ---- 身份/诊断 ----

    @Override
    public boolean equals(Object object) {
        if (!(object instanceof ContentValues)) {
            return false;
        }
        return mValues.equals(((ContentValues) object).mValues);
    }

    @Override
    public int hashCode() {
        return mValues.hashCode();
    }

    @Override
    public String toString() {
        return "ContentValues[" + mValues + "]";
    }
}
