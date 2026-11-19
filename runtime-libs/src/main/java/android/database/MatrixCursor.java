package android.database;

import java.util.ArrayList;

/**
 * 内存行表 Cursor（AOSP android.database.MatrixCursor 公开面；§31.7 C 档
 * POJO→lib 真语义——SQLite 骨架 query 返回体 / 通用内存查询结果）。
 * 观察者/通知面 = 记录式空操作（AOSP：无 ContentService 泵时不产生回调）。
 */
public class MatrixCursor extends AbstractCursor {
    private final String[] mColumnNames;
    private final int mColumnCount;
    private final ArrayList<Object[]> mRows = new ArrayList<Object[]>();

    public MatrixCursor(String[] columnNames) {
        this(columnNames, 16);
    }

    public MatrixCursor(String[] columnNames, int initialCapacity) {
        mColumnNames = columnNames;
        mColumnCount = columnNames.length;
    }

    public void addRow(Object[] columnValues) {
        if (columnValues.length != mColumnCount) {
            throw new IllegalArgumentException("columnNames.length = "
                    + mColumnCount + ", columnValues.length = " + columnValues.length);
        }
        mRows.add(columnValues);
    }

    public void addRow(Iterable<?> columnValues) {
        Object[] row = new Object[mColumnCount];
        int i = 0;
        for (Object v : columnValues) {
            if (i >= mColumnCount) {
                throw new IllegalArgumentException("columnValues.size > " + mColumnCount);
            }
            row[i++] = v;
        }
        if (i != mColumnCount) {
            throw new IllegalArgumentException("columnValues.size < " + mColumnCount);
        }
        mRows.add(row);
    }

    public RowBuilder newRow() {
        return new RowBuilder();
    }

    @Override
    public int getCount() {
        return mRows.size();
    }

    @Override
    public String[] getColumnNames() {
        return mColumnNames;
    }

    @Override
    public String getString(int column) {
        Object v = getRaw(column);
        return v != null ? v.toString() : null;
    }

    @Override
    public short getShort(int column) {
        Object v = getRaw(column);
        return (v instanceof Number) ? ((Number) v).shortValue() : 0;
    }

    @Override
    public int getInt(int column) {
        Object v = getRaw(column);
        return (v instanceof Number) ? ((Number) v).intValue() : 0;
    }

    @Override
    public long getLong(int column) {
        Object v = getRaw(column);
        return (v instanceof Number) ? ((Number) v).longValue() : 0L;
    }

    @Override
    public float getFloat(int column) {
        Object v = getRaw(column);
        return (v instanceof Number) ? ((Number) v).floatValue() : 0f;
    }

    @Override
    public double getDouble(int column) {
        Object v = getRaw(column);
        return (v instanceof Number) ? ((Number) v).doubleValue() : 0d;
    }

    @Override
    public byte[] getBlob(int column) {
        Object v = getRaw(column);
        return (v instanceof byte[]) ? (byte[]) v : null;
    }

    @Override
    public int getType(int column) {
        Object v = getRaw(column);
        if (v == null) {
            return Cursor.FIELD_TYPE_NULL;
        }
        if (v instanceof byte[]) {
            return Cursor.FIELD_TYPE_BLOB;
        }
        if (v instanceof Float || v instanceof Double) {
            return Cursor.FIELD_TYPE_FLOAT;
        }
        if (v instanceof Number) {
            return Cursor.FIELD_TYPE_INTEGER;
        }
        return Cursor.FIELD_TYPE_STRING;
    }

    @Override
    public boolean isNull(int column) {
        return getRaw(column) == null;
    }

    private Object getRaw(int column) {
        if (mPos < 0 || mPos >= mRows.size()) {
            return null;
        }
        Object[] row = mRows.get(mPos);
        return (column < 0 || column >= row.length) ? null : row[column];
    }

    /** 行构造器（AOSP MatrixCursor.RowBuilder：逐列 add 后入表）。 */
    public class RowBuilder {
        private final Object[] mRow = new Object[mColumnCount];
        private int mIndex = 0;

        public RowBuilder add(Object columnValue) {
            if (mIndex == mColumnCount) {
                throw new IllegalArgumentException("No more columns left.");
            }
            mRow[mIndex++] = columnValue;
            return this;
        }

        public RowBuilder add(String columnName, Object value) {
            for (int i = 0; i < mColumnNames.length; i++) {
                if (columnName.equals(mColumnNames[i])) {
                    mRow[i] = value;
                    return this;
                }
            }
            throw new IllegalArgumentException("Column '" + columnName + "' does not exist.");
        }
    }
}
