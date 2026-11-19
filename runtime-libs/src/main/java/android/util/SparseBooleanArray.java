package android.util;

/**
 * int 键布尔值稀疏表（AOSP android.util.SparseBooleanArray 公开面；§31.7 C 档
 * lib POJO——纯逻辑，二分插入序）。AppCompat ActionMenuPresenter/Toolbar 菜单
 * 面必需（缺类曾使 ZHToolBar 构造整体 NCDFE 失败）。
 */
public class SparseBooleanArray implements Cloneable {
    private int[] mKeys;
    private boolean[] mValues;
    private int mSize;

    public SparseBooleanArray() {
        this(0);
    }

    public SparseBooleanArray(int initialCapacity) {
        if (initialCapacity == 0) {
            this.mKeys = SparseIntArray.EmptyArrays.INT;
            this.mValues = new boolean[0];
        } else {
            this.mKeys = new int[initialCapacity];
            this.mValues = new boolean[initialCapacity];
        }
        this.mSize = 0;
    }

    @Override
    public SparseBooleanArray clone() {
        SparseBooleanArray clone = null;
        try {
            clone = (SparseBooleanArray) super.clone();
            clone.mKeys = this.mKeys.clone();
            clone.mValues = this.mValues.clone();
        } catch (CloneNotSupportedException e) {
            // Object.clone 保证可用
        }
        return clone;
    }

    public boolean get(int key) {
        return get(key, false);
    }

    public boolean get(int key, boolean valueIfKeyNotFound) {
        int i = binarySearch(this.mKeys, this.mSize, key);
        return i < 0 ? valueIfKeyNotFound : this.mValues[i];
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

    public void removeAt(int index) {
        System.arraycopy(this.mKeys, index + 1, this.mKeys, index, this.mSize - (index + 1));
        System.arraycopy(this.mValues, index + 1, this.mValues, index, this.mSize - (index + 1));
        this.mSize--;
    }

    public void put(int key, boolean value) {
        int i = binarySearch(this.mKeys, this.mSize, key);
        if (i >= 0) {
            this.mValues[i] = value;
        } else {
            i = ~i;
            if (this.mSize >= this.mKeys.length) {
                int nlen = growSize(this.mSize);
                int[] nkeys = new int[nlen];
                boolean[] nvalues = new boolean[nlen];
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

    public void append(int key, boolean value) {
        if (this.mSize != 0 && key <= this.mKeys[this.mSize - 1]) {
            put(key, value);
            return;
        }
        if (this.mSize >= this.mKeys.length) {
            int nlen = growSize(this.mSize);
            int[] nkeys = new int[nlen];
            boolean[] nvalues = new boolean[nlen];
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

    public boolean valueAt(int index) {
        return this.mValues[index];
    }

    public void setValueAt(int index, boolean value) {
        this.mValues[index] = value;
    }

    public int indexOfKey(int key) {
        return binarySearch(this.mKeys, this.mSize, key);
    }

    public int indexOfValue(boolean value) {
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
}
