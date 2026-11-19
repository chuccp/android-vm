package android.util;

/**
 * int 键对象值稀疏表（AOSP android.util.SparseArray 公开面；§31.7 C 档 lib
 * POJO——纯逻辑，二分插入序）。
 */
public class SparseArray<E> implements Cloneable {
    private int[] mKeys;
    private Object[] mValues;
    private int mSize;

    public SparseArray() {
        this(0);
    }

    public SparseArray(int initialCapacity) {
        if (initialCapacity == 0) {
            this.mKeys = SparseIntArray.EmptyArrays.INT;
            this.mValues = SparseIntArray.EmptyArrays.OBJECT;
        } else {
            this.mKeys = new int[initialCapacity];
            this.mValues = new Object[initialCapacity];
        }
        this.mSize = 0;
    }

    @Override
    @SuppressWarnings("unchecked")
    public SparseArray<E> clone() {
        SparseArray<E> clone = null;
        try {
            clone = (SparseArray<E>) super.clone();
            clone.mKeys = this.mKeys.clone();
            clone.mValues = this.mValues.clone();
        } catch (CloneNotSupportedException e) {
            // Object.clone 保证可用
        }
        return clone;
    }

    @SuppressWarnings("unchecked")
    public E get(int key) {
        int i = binarySearch(this.mKeys, this.mSize, key);
        if (i < 0) return null;
        return (E) this.mValues[i];
    }

    @SuppressWarnings("unchecked")
    public E get(int key, E valueIfKeyNotFound) {
        int i = binarySearch(this.mKeys, this.mSize, key);
        if (i < 0) return valueIfKeyNotFound;
        return (E) this.mValues[i];
    }

    public void delete(int key) {
        int i = binarySearch(this.mKeys, this.mSize, key);
        if (i >= 0) {
            Object[] values = this.mValues;
            values[i] = null;
            System.arraycopy(this.mKeys, i + 1, this.mKeys, i, this.mSize - (i + 1));
            System.arraycopy(values, i + 1, values, i, this.mSize - (i + 1));
            this.mSize--;
        }
    }

    public void remove(int key) {
        delete(key);
    }

    public void removeAt(int index) {
        Object[] values = this.mValues;
        values[index] = null;
        System.arraycopy(this.mKeys, index + 1, this.mKeys, index, this.mSize - (index + 1));
        System.arraycopy(values, index + 1, values, index, this.mSize - (index + 1));
        this.mSize--;
    }

    public void put(int key, E value) {
        int i = binarySearch(this.mKeys, this.mSize, key);
        if (i >= 0) {
            this.mValues[i] = value;
        } else {
            i = ~i;
            if (this.mSize >= this.mKeys.length) {
                int nlen = growSize(this.mSize);
                int[] nkeys = new int[nlen];
                Object[] nvalues = new Object[nlen];
                System.arraycopy(this.mKeys, 0, nkeys, 0, i);
                System.arraycopy(this.mValues, 0, nvalues, 0, i);
                System.arraycopy(this.mKeys, i, nkeys, i + 1, this.mSize - i);
                System.arraycopy(this.mValues, i, nvalues, i + 1, this.mSize - i);
                this.mKeys = nkeys;
                this.mValues = nvalues;
            } else {
                System.arraycopy(this.mKeys, i, this.mKeys, i + 1, this.mSize - i);
                System.arraycopy(this.mValues, i, this.mValues, i + 1, this.mSize - i);
            }
            this.mKeys[i] = key;
            this.mValues[i] = value;
            this.mSize++;
        }
    }

    public void append(int key, E value) {
        if (this.mSize != 0 && key <= this.mKeys[this.mSize - 1]) {
            put(key, value);
            return;
        }
        if (this.mSize >= this.mKeys.length) {
            int nlen = growSize(this.mSize);
            int[] nkeys = new int[nlen];
            Object[] nvalues = new Object[nlen];
            System.arraycopy(this.mKeys, 0, nkeys, 0, this.mSize);
            System.arraycopy(this.mValues, 0, nvalues, 0, this.mSize);
            this.mKeys = nkeys;
            this.mValues = nvalues;
        }
        this.mKeys[this.mSize] = key;
        this.mValues[this.mSize] = value;
        this.mSize++;
    }

    public int size() {
        return this.mSize;
    }

    public int keyAt(int index) {
        return this.mKeys[index];
    }

    @SuppressWarnings("unchecked")
    public E valueAt(int index) {
        return (E) this.mValues[index];
    }

    public void setValueAt(int index, E value) {
        this.mValues[index] = value;
    }

    public int indexOfKey(int key) {
        return binarySearch(this.mKeys, this.mSize, key);
    }

    public int indexOfValue(E value) {
        for (int i = 0; i < this.mSize; i++) {
            if (this.mValues[i] == value) return i;
        }
        return -1;
    }

    public void clear() {
        for (int i = 0; i < this.mSize; i++) {
            this.mValues[i] = null;
        }
        this.mSize = 0;
    }

    private static int growSize(int current) {
        return current <= 4 ? 8 : current * 2;
    }

    private static int binarySearch(int[] array, int size, int key) {
        int lo = 0;
        int hi = size - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int midVal = array[mid];
            if (midVal < key) {
                lo = mid + 1;
            } else if (midVal > key) {
                hi = mid - 1;
            } else {
                return mid;
            }
        }
        return ~lo;
    }

    @Override
    public String toString() {
        if (size() <= 0) return "{}";
        StringBuilder sb = new StringBuilder(this.mSize * 28);
        sb.append('{');
        for (int i = 0; i < this.mSize; i++) {
            if (i > 0) sb.append(", ");
            sb.append(this.mKeys[i]).append('=').append(this.mValues[i]);
        }
        return sb.append('}').toString();
    }
}
