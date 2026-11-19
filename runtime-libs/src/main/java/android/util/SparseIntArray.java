package android.util;

/**
 * int 键 int 值稀疏表（AOSP android.util.SparseIntArray 公开面；§31.7 C 档 lib
 * POJO——纯逻辑，二分插入序）。
 */
public class SparseIntArray implements Cloneable {
    private int[] mKeys;
    private int[] mValues;
    private int mSize;

    public SparseIntArray() {
        this(0);
    }

    public SparseIntArray(int initialCapacity) {
        if (initialCapacity == 0) {
            this.mKeys = EmptyArrays.INT;
            this.mValues = EmptyArrays.INT;
        } else {
            this.mKeys = new int[initialCapacity];
            this.mValues = new int[initialCapacity];
        }
        this.mSize = 0;
    }

    @Override
    public SparseIntArray clone() {
        SparseIntArray clone = null;
        try {
            clone = (SparseIntArray) super.clone();
            clone.mKeys = this.mKeys.clone();
            clone.mValues = this.mValues.clone();
        } catch (CloneNotSupportedException e) {
            // Object.clone 保证可用
        }
        return clone;
    }

    public int get(int key) {
        return get(key, 0);
    }

    public int get(int key, int valueIfKeyNotFound) {
        int i = binarySearch(this.mKeys, this.mSize, key);
        if (i < 0) return valueIfKeyNotFound;
        return this.mValues[i];
    }

    public void delete(int key) {
        int i = binarySearch(this.mKeys, this.mSize, key);
        if (i >= 0) {
            System.arraycopy(this.mKeys, i + 1, this.mKeys, i, this.mSize - (i + 1));
            System.arraycopy(this.mValues, i + 1, this.mValues, i, this.mSize - (i + 1));
            this.mSize--;
        }
    }

    public void remove(int key) {
        delete(key);
    }

    public void put(int key, int value) {
        int i = binarySearch(this.mKeys, this.mSize, key);
        if (i >= 0) {
            this.mValues[i] = value;
        } else {
            i = ~i;
            if (this.mSize >= this.mKeys.length) {
                int nlen = growSize(this.mSize);
                int[] nkeys = new int[nlen];
                int[] nvalues = new int[nlen];
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

    /** 追加（键必须单调递增，AOSP append 语义；否则退化为 put）。 */
    public void append(int key, int value) {
        if (this.mSize != 0 && key <= this.mKeys[this.mSize - 1]) {
            put(key, value);
            return;
        }
        if (this.mSize >= this.mKeys.length) {
            int nlen = growSize(this.mSize);
            int[] nkeys = new int[nlen];
            int[] nvalues = new int[nlen];
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

    public int valueAt(int index) {
        return this.mValues[index];
    }

    public void setValueAt(int index, int value) {
        this.mValues[index] = value;
    }

    public int indexOfKey(int key) {
        return binarySearch(this.mKeys, this.mSize, key);
    }

    public int indexOfValue(int value) {
        for (int i = 0; i < this.mSize; i++) {
            if (this.mValues[i] == value) return i;
        }
        return -1;
    }

    public void clear() {
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

    /** 空数组常量（包内共享）。 */
    static final class EmptyArrays {
        static final int[] INT = new int[0];
        static final Object[] OBJECT = new Object[0];

        private EmptyArrays() {
        }
    }
}
