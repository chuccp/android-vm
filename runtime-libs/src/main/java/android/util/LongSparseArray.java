package android.util;

/**
 * android.util.LongSparseArray（AOSP 公开面；long 键的稀疏映射）。
 *
 * <p>按 AOSP 语义实现：键升序数组 + 二分查找；put/get/delete/append/size/
 * keyAt/valueAt/indexOf 家族；值可为 null。
 */
public class LongSparseArray<E> implements Cloneable {
    private static final Object DELETED = new Object();
    private boolean mGarbage = false;

    private long[] mKeys;
    private Object[] mValues;
    private int mSize;

    public LongSparseArray() {
        this(10);
    }

    public LongSparseArray(int initialCapacity) {
        if (initialCapacity == 0) {
            mKeys = new long[0];
            mValues = new Object[0];
        } else {
            mKeys = new long[initialCapacity];
            mValues = new Object[initialCapacity];
        }
        mSize = 0;
    }

    @Override
    @SuppressWarnings("unchecked")
    public LongSparseArray<E> clone() {
        // 手动深拷贝（不依赖 Object.clone intrinsic）
        LongSparseArray<E> clone = new LongSparseArray<E>(0);
        clone.mKeys = new long[mKeys.length];
        clone.mValues = new Object[mValues.length];
        System.arraycopy(mKeys, 0, clone.mKeys, 0, mKeys.length);
        System.arraycopy(mValues, 0, clone.mValues, 0, mValues.length);
        clone.mSize = mSize;
        clone.mGarbage = mGarbage;
        return clone;
    }

    private static int binarySearch(long[] array, int size, long value) {
        int lo = 0;
        int hi = size - 1;
        while (lo <= hi) {
            final int mid = (lo + hi) >>> 1;
            final long midVal = array[mid];
            if (midVal < value) {
                lo = mid + 1;
            } else if (midVal > value) {
                hi = mid - 1;
            } else {
                return mid;
            }
        }
        return ~lo;
    }

    public E get(long key) {
        return get(key, null);
    }

    @SuppressWarnings("unchecked")
    public E get(long key, E valueIfKeyNotFound) {
        int i = binarySearch(mKeys, mSize, key);
        if (i < 0 || mValues[i] == DELETED) {
            return valueIfKeyNotFound;
        }
        return (E) mValues[i];
    }

    public void delete(long key) {
        int i = binarySearch(mKeys, mSize, key);
        if (i >= 0 && mValues[i] != DELETED) {
            mValues[i] = DELETED;
            mGarbage = true;
        }
    }

    /** @deprecated AOSP：用 {@link #delete(long)}。 */
    @Deprecated
    public void remove(long key) {
        delete(key);
    }

    public void removeAt(int index) {
        if (mValues[index] != DELETED) {
            mValues[index] = DELETED;
            mGarbage = true;
        }
    }

    private void gc() {
        if (!mGarbage) return;
        int o = 0;
        for (int i = 0; i < mSize; i++) {
            if (mValues[i] != DELETED) {
                mKeys[o] = mKeys[i];
                mValues[o] = mValues[i];
                o++;
            }
        }
        for (int i = o; i < mSize; i++) {
            mValues[i] = null;
        }
        mSize = o;
        mGarbage = false;
    }

    public void put(long key, E value) {
        int i = binarySearch(mKeys, mSize, key);
        if (i >= 0) {
            mValues[i] = value;
        } else {
            i = ~i;
            if (i < mSize && mValues[i] == DELETED) {
                mKeys[i] = key;
                mValues[i] = value;
                return;
            }
            if (mGarbage && mSize >= mKeys.length) {
                gc();
                i = ~binarySearch(mKeys, mSize, key);
            }
            if (mSize >= mKeys.length) {
                int n = Math.max(mSize + 1, mKeys.length * 2);
                long[] nkeys = new long[n];
                Object[] nvalues = new Object[n];
                System.arraycopy(mKeys, 0, nkeys, 0, mSize);
                System.arraycopy(mValues, 0, nvalues, 0, mSize);
                mKeys = nkeys;
                mValues = nvalues;
            }
            if (mSize - i != 0) {
                System.arraycopy(mKeys, i, mKeys, i + 1, mSize - i);
                System.arraycopy(mValues, i, mValues, i + 1, mSize - i);
            }
            mKeys[i] = key;
            mValues[i] = value;
            mSize++;
        }
    }

    public int size() {
        if (mGarbage) gc();
        return mSize;
    }

    public boolean containsKey(long key) {
        return indexOfKey(key) >= 0;
    }

    public int indexOfKey(long key) {
        if (mGarbage) gc();
        return binarySearch(mKeys, mSize, key);
    }

    public int indexOfValue(E value) {
        if (mGarbage) gc();
        for (int i = 0; i < mSize; i++) {
            if (mValues[i] == value) return i;
        }
        return -1;
    }

    public long keyAt(int index) {
        if (mGarbage) gc();
        return mKeys[index];
    }

    @SuppressWarnings("unchecked")
    public E valueAt(int index) {
        if (mGarbage) gc();
        return (E) mValues[index];
    }

    public void setValueAt(int index, E value) {
        if (mGarbage) gc();
        mValues[index] = value;
    }

    public void append(long key, E value) {
        if (mSize != 0 && key <= mKeys[mSize - 1]) {
            put(key, value);
            return;
        }
        if (mGarbage && mSize >= mKeys.length) gc();
        if (mSize >= mKeys.length) {
            int n = Math.max(mSize + 1, mKeys.length * 2);
            long[] nkeys = new long[n];
            Object[] nvalues = new Object[n];
            System.arraycopy(mKeys, 0, nkeys, 0, mSize);
            System.arraycopy(mValues, 0, nvalues, 0, mSize);
            mKeys = nkeys;
            mValues = nvalues;
        }
        mKeys[mSize] = key;
        mValues[mSize] = value;
        mSize++;
    }

    public void clear() {
        int n = mSize;
        Object[] values = mValues;
        for (int i = 0; i < n; i++) {
            values[i] = null;
        }
        mSize = 0;
        mGarbage = false;
    }

    @Override
    public String toString() {
        if (size() <= 0) {
            return "{}";
        }
        StringBuilder buffer = new StringBuilder(mSize * 28);
        buffer.append('{');
        for (int i = 0; i < mSize; i++) {
            if (i > 0) buffer.append(", ");
            buffer.append(keyAt(i));
            buffer.append('=');
            Object value = valueAt(i);
            if (value != this) {
                buffer.append(value);
            } else {
                buffer.append("(this Map)");
            }
        }
        buffer.append('}');
        return buffer.toString();
    }
}
