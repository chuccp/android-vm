package android.os;

import java.io.FileDescriptor;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * 数据打包载体（AOSP android.os.Parcel 公开面）。
 *
 * <p>§31.7 骨架判定（可执行不崩、语义合理；状态存字段）：进程内变体队列——
 * writeX 存值 / readX 读回（类型错配抛 RuntimeException）。Parcelable 面 =
 * 进程内引用传递（writeParcelable 存对象本体，readParcelable 原样取回；同进程
 * 语义等价，跨进程 marshall/Creator 重建缝后置）。Typed 族（writeTypedList/
 * createTypedArrayList 等）按值表搬运，Creator 重建缝同后置。binder/fd 族
 * （writeStrongBinder/writeFileDescriptor 等）= binder 缝面，本骨架抛
 * UnsupportedOperationException（缺口显式）。
 */
public final class Parcel {
    private static final Object sPoolLock = new Object();
    private static Parcel sPool;

    private final ArrayList<Object> mItems = new ArrayList<Object>();
    private int mPos;

    private Parcel() {
    }

    public static Parcel obtain() {
        synchronized (sPoolLock) {
            if (sPool != null) {
                Parcel p = sPool;
                sPool = null;
                p.recycleThis();
                return p;
            }
        }
        return new Parcel();
    }

    public void recycle() {
        synchronized (sPoolLock) {
            if (sPool == null) {
                recycleThis();
                sPool = this;
            }
        }
    }

    private void recycleThis() {
        mItems.clear();
        mPos = 0;
    }

    // ---- 位置面 ----

    public int dataSize() {
        return mItems.size();
    }

    public int dataAvail() {
        return mItems.size() - mPos;
    }

    public int dataPosition() {
        return mPos;
    }

    public void setDataPosition(int pos) {
        mPos = pos;
    }

    public void appendFrom(Parcel src, int offset, int count) {
        for (int i = 0; i < count; i++) {
            mItems.add(src.mItems.get(offset + i));
        }
    }

    // ---- 基本型 ----

    private void write(Object value) {
        mItems.add(value);
    }

    private Object read() {
        if (mPos >= mItems.size()) {
            throw new RuntimeException("Parcel: read past end");
        }
        return mItems.get(mPos++);
    }

    private Object readOrNull() {
        return (mPos >= mItems.size()) ? null : mItems.get(mPos++);
    }

    public void writeByte(byte value) {
        write(value);
    }

    public byte readByte() {
        Object v = read();
        return (v instanceof Byte) ? (Byte) v : 0;
    }

    public void writeInt(int value) {
        write(value);
    }

    public int readInt() {
        Object v = read();
        return (v instanceof Integer) ? (Integer) v : 0;
    }

    public void writeLong(long value) {
        write(value);
    }

    public long readLong() {
        Object v = read();
        return (v instanceof Long) ? (Long) v : 0L;
    }

    public void writeFloat(float value) {
        write(value);
    }

    public float readFloat() {
        Object v = read();
        return (v instanceof Float) ? (Float) v : 0f;
    }

    public void writeDouble(double value) {
        write(value);
    }

    public double readDouble() {
        Object v = read();
        return (v instanceof Double) ? (Double) v : 0d;
    }

    public void writeString(String value) {
        write(value);
    }

    public String readString() {
        Object v = readOrNull();
        return (v instanceof String) ? (String) v : null;
    }

    public void writeByteArray(byte[] b) {
        write(b);
    }

    public void writeByteArray(byte[] b, int offset, int len) {
        byte[] copy = new byte[len];
        if (b != null) {
            System.arraycopy(b, offset, copy, 0, len);
        }
        write(copy);
    }

    public byte[] createByteArray() {
        Object v = readOrNull();
        return (v instanceof byte[]) ? (byte[]) v : null;
    }

    public void writeBooleanArray(boolean[] b) {
        write(b);
    }

    public boolean[] createBooleanArray() {
        Object v = readOrNull();
        return (v instanceof boolean[]) ? (boolean[]) v : null;
    }

    public void writeCharArray(char[] b) {
        write(b);
    }

    public char[] createCharArray() {
        Object v = readOrNull();
        return (v instanceof char[]) ? (char[]) v : null;
    }

    public void writeIntArray(int[] b) {
        write(b);
    }

    public int[] createIntArray() {
        Object v = readOrNull();
        return (v instanceof int[]) ? (int[]) v : null;
    }

    public void writeLongArray(long[] b) {
        write(b);
    }

    public long[] createLongArray() {
        Object v = readOrNull();
        return (v instanceof long[]) ? (long[]) v : null;
    }

    public void writeFloatArray(float[] b) {
        write(b);
    }

    public float[] createFloatArray() {
        Object v = readOrNull();
        return (v instanceof float[]) ? (float[]) v : null;
    }

    public void writeDoubleArray(double[] b) {
        write(b);
    }

    public double[] createDoubleArray() {
        Object v = readOrNull();
        return (v instanceof double[]) ? (double[]) v : null;
    }

    public void writeStringArray(String[] b) {
        write(b);
    }

    public String[] createStringArray() {
        Object v = readOrNull();
        return (v instanceof String[]) ? (String[]) v : null;
    }

    // ---- 容器面 ----

    public void writeList(List<?> value) {
        write(value);
    }

    public void readList(List<?> outVal, ClassLoader loader) {
        Object v = readOrNull();
        if (v instanceof List && outVal != null) {
            @SuppressWarnings("unchecked")
            List<Object> dst = (List<Object>) outVal;
            dst.clear();
            dst.addAll((List<Object>) v);
        }
    }

    public ArrayList<?> createStringArrayList() {
        Object v = readOrNull();
        return (v instanceof ArrayList) ? (ArrayList<?>) v : null;
    }

    public void writeStringList(List<String> value) {
        write(value);
    }

    public void writeMap(Map<?, ?> value) {
        write(value);
    }

    public void readMap(Map<?, ?> outVal, ClassLoader loader) {
        Object v = readOrNull();
        if (v instanceof Map && outVal != null) {
            @SuppressWarnings("unchecked")
            Map<Object, Object> dst = (Map<Object, Object>) outVal;
            dst.clear();
            dst.putAll((Map<Object, Object>) v);
        }
    }

    public java.util.HashMap readHashMap(ClassLoader loader) {
        Object v = readOrNull();
        return (v instanceof java.util.HashMap) ? (java.util.HashMap) v : null;
    }

    public void writeArray(Object[] value) {
        write(value);
    }

    public Object[] readArray(ClassLoader loader) {
        Object v = readOrNull();
        return (v instanceof Object[]) ? (Object[]) v : null;
    }

    public void writeValue(Object v) {
        write(v);
    }

    public Object readValue(ClassLoader loader) {
        return readOrNull();
    }

    // ---- Parcelable 面（进程内引用传递；Creator 重建缝后置）----

    public void writeParcelable(Parcelable p, int parcelableFlags) {
        write(p);
    }

    public <T extends Parcelable> T readParcelable(ClassLoader loader) {
        Object v = readOrNull();
        @SuppressWarnings("unchecked")
        T out = (T) v;
        return out;
    }

    public <T extends Parcelable> T readTypedObject(Parcelable.Creator<T> c) {
        Object v = readOrNull();
        @SuppressWarnings("unchecked")
        T out = (T) v;
        return out;
    }

    public <T extends Parcelable> void writeTypedObject(T val, int parcelableFlags) {
        write(val);
    }

    public <T extends Parcelable> void writeTypedList(List<T> val) {
        write(val);
    }

    public <T> ArrayList<T> createTypedArrayList(Parcelable.Creator<T> c) {
        Object v = readOrNull();
        @SuppressWarnings("unchecked")
        ArrayList<T> out = (ArrayList<T>) v;
        return out;
    }

    public <T extends Parcelable> void writeTypedArray(T[] val, int parcelableFlags) {
        write(val);
    }

    public <T> T[] createTypedArray(Parcelable.Creator<T> c) {
        Object v = readOrNull();
        @SuppressWarnings("unchecked")
        T[] out = (T[]) v;
        return out;
    }

    public void writeParcelableArray(Parcelable[] value, int parcelableFlags) {
        write(value);
    }

    public Parcelable[] readParcelableArray(ClassLoader loader) {
        Object v = readOrNull();
        return (v instanceof Parcelable[]) ? (Parcelable[]) v : null;
    }

    public Parcelable.Creator<?> readParcelableCreator(ClassLoader loader) {
        Object v = readOrNull();
        return (v instanceof Parcelable.Creator) ? (Parcelable.Creator<?>) v : null;
    }

    public <T extends Parcelable> List<T> readParcelableList(List<T> list, ClassLoader loader) {
        Object v = readOrNull();
        if (v instanceof List && list != null) {
            @SuppressWarnings("unchecked")
            List<T> src = (List<T>) v;
            list.clear();
            list.addAll(src);
            return list;
        }
        return null;
    }

    // ---- Bundle/Serializable/稀疏面 ----

    public void writeBundle(Bundle value) {
        write(value);
    }

    public Bundle readBundle() {
        Object v = readOrNull();
        return (v instanceof Bundle) ? (Bundle) v : null;
    }

    public Bundle readBundle(ClassLoader loader) {
        return readBundle();
    }

    public void writeSerializable(java.io.Serializable s) {
        write(s);
    }

    public java.io.Serializable readSerializable() {
        Object v = readOrNull();
        return (v instanceof java.io.Serializable) ? (java.io.Serializable) v : null;
    }

    public void writeSparseArray(android.util.SparseArray<?> value) {
        write(value);
    }

    public <T> android.util.SparseArray<T> readSparseArray(ClassLoader loader) {
        Object v = readOrNull();
        @SuppressWarnings("unchecked")
        android.util.SparseArray<T> out = (android.util.SparseArray<T>) v;
        return out;
    }

    public void writeSparseBooleanArray(android.util.SparseBooleanArray value) {
        write(value);
    }

    public android.util.SparseBooleanArray readSparseBooleanArray() {
        Object v = readOrNull();
        return (v instanceof android.util.SparseBooleanArray)
                ? (android.util.SparseBooleanArray) v : null;
    }

    // ---- 异常协议面（AOSP：writeNoException 空操作 / readException 透传）----

    public void writeNoException() {
    }

    public void readException() {
    }

    public void readException(int code, String msg) {
        if (code != 0) {
            throw new RuntimeException("Parcel exception code=" + code + " msg=" + msg);
        }
    }

    // ---- 接口令牌面 ----

    public void writeInterfaceToken(String interfaceName) {
        write(interfaceName);
    }

    public void enforceInterface(String interfaceName) {
        Object v = readOrNull();
        if (v instanceof String && !interfaceName.equals(v)) {
            throw new SecurityException("Binder invocation to " + v + " got " + interfaceName);
        }
    }

    // ---- binder/fd/marshall 缝面（显式缺口）----

    public void writeStrongBinder(IBinder binder) {
        throw new UnsupportedOperationException("Parcel binder seam deferred");
    }

    public IBinder readStrongBinder() {
        throw new UnsupportedOperationException("Parcel binder seam deferred");
    }

    public void writeStrongInterface(IInterface intf) {
        throw new UnsupportedOperationException("Parcel binder seam deferred");
    }

    public void writeBinderArray(IBinder[] value) {
        throw new UnsupportedOperationException("Parcel binder seam deferred");
    }

    public IBinder[] createBinderArray() {
        throw new UnsupportedOperationException("Parcel binder seam deferred");
    }

    public void writeBinderList(List<IBinder> value) {
        throw new UnsupportedOperationException("Parcel binder seam deferred");
    }

    public ArrayList<IBinder> createBinderArrayList() {
        throw new UnsupportedOperationException("Parcel binder seam deferred");
    }

    public void writeFileDescriptor(FileDescriptor fd) {
        throw new UnsupportedOperationException("Parcel fd seam deferred");
    }

    public FileDescriptor readFileDescriptor() {
        throw new UnsupportedOperationException("Parcel fd seam deferred");
    }

    public byte[] marshall() {
        throw new UnsupportedOperationException("Parcel marshall seam deferred");
    }

    public void unmarshall(byte[] data, int offset, int length) {
        throw new UnsupportedOperationException("Parcel marshall seam deferred");
    }
}
