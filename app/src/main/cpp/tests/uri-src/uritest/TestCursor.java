package uritest;

import android.database.CharArrayBuffer;
import android.database.ContentObserver;
import android.database.Cursor;
import android.database.DataSetObserver;
import android.net.Uri;
import android.os.Bundle;

import java.util.List;

/**
 * 最小内存 Cursor 实现（android.database.Cursor 全接口面；fixture 查询返回体）。
 * 单表多行；类型按值类自然映射。观察者/通知面 = 记录式空操作（AOSP 语义：
 * 无 ContentService 泵时不产生回调）。
 */
public class TestCursor implements Cursor {
    private final String[] mColumns;
    private final Object[][] mRows;
    private int mPos = -1;
    private boolean mClosed = false;
    private Uri mNotifyUri;
    private Bundle mExtras = new Bundle();

    public TestCursor(String[] columns, Object[][] rows) {
        mColumns = columns;
        mRows = rows;
    }

    @Override
    public int getCount() {
        return mRows.length;
    }

    @Override
    public int getPosition() {
        return mPos;
    }

    @Override
    public boolean move(int offset) {
        return moveToPosition(mPos + offset);
    }

    @Override
    public boolean moveToPosition(int position) {
        if (position < -1) {
            mPos = -1;
            return false;
        }
        if (position > mRows.length) {
            mPos = mRows.length;
            return false;
        }
        mPos = position;
        return position >= 0 && position < mRows.length;
    }

    @Override
    public boolean moveToFirst() {
        return moveToPosition(0);
    }

    @Override
    public boolean moveToLast() {
        return moveToPosition(mRows.length - 1);
    }

    @Override
    public boolean moveToNext() {
        return moveToPosition(mPos + 1);
    }

    @Override
    public boolean moveToPrevious() {
        return moveToPosition(mPos - 1);
    }

    @Override
    public boolean isFirst() {
        return mPos == 0 && mRows.length > 0;
    }

    @Override
    public boolean isLast() {
        return mRows.length > 0 && mPos == mRows.length - 1;
    }

    @Override
    public boolean isBeforeFirst() {
        return mRows.length == 0 || mPos < 0;
    }

    @Override
    public boolean isAfterLast() {
        return mRows.length == 0 || mPos >= mRows.length;
    }

    @Override
    public int getColumnIndex(String columnName) {
        for (int i = 0; i < mColumns.length; i++) {
            if (mColumns[i].equals(columnName)) {
                return i;
            }
        }
        return -1;
    }

    @Override
    public int getColumnIndexOrThrow(String columnName) {
        int i = getColumnIndex(columnName);
        if (i < 0) {
            throw new IllegalArgumentException("column '" + columnName + "' does not exist");
        }
        return i;
    }

    @Override
    public String getColumnName(int columnIndex) {
        return mColumns[columnIndex];
    }

    @Override
    public String[] getColumnNames() {
        return mColumns;
    }

    @Override
    public int getColumnCount() {
        return mColumns.length;
    }

    private Object cell(int columnIndex) {
        if (mPos < 0 || mPos >= mRows.length) {
            return null;
        }
        return mRows[mPos][columnIndex];
    }

    @Override
    public byte[] getBlob(int columnIndex) {
        Object v = cell(columnIndex);
        return (v instanceof byte[]) ? (byte[]) v : null;
    }

    @Override
    public String getString(int columnIndex) {
        Object v = cell(columnIndex);
        return v != null ? v.toString() : null;
    }

    @Override
    public void copyStringToBuffer(int columnIndex, CharArrayBuffer buffer) {
        String s = getString(columnIndex);
        if (buffer != null && s != null) {
            buffer.data = s.toCharArray();
            buffer.sizeCopied = s.length();
        }
    }

    @Override
    public short getShort(int columnIndex) {
        Object v = cell(columnIndex);
        return (v instanceof Number) ? ((Number) v).shortValue() : 0;
    }

    @Override
    public int getInt(int columnIndex) {
        Object v = cell(columnIndex);
        return (v instanceof Number) ? ((Number) v).intValue() : 0;
    }

    @Override
    public long getLong(int columnIndex) {
        Object v = cell(columnIndex);
        return (v instanceof Number) ? ((Number) v).longValue() : 0L;
    }

    @Override
    public float getFloat(int columnIndex) {
        Object v = cell(columnIndex);
        return (v instanceof Number) ? ((Number) v).floatValue() : 0f;
    }

    @Override
    public double getDouble(int columnIndex) {
        Object v = cell(columnIndex);
        return (v instanceof Number) ? ((Number) v).doubleValue() : 0d;
    }

    @Override
    public int getType(int columnIndex) {
        Object v = cell(columnIndex);
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
    public boolean isNull(int columnIndex) {
        return cell(columnIndex) == null;
    }

    @Override
    public void close() {
        mClosed = true;
    }

    @Override
    public boolean isClosed() {
        return mClosed;
    }

    @Override
    public void deactivate() {
    }

    @Override
    public boolean requery() {
        return !mClosed;
    }

    @Override
    public void registerContentObserver(ContentObserver observer) {
    }

    @Override
    public void unregisterContentObserver(ContentObserver observer) {
    }

    @Override
    public void registerDataSetObserver(DataSetObserver observer) {
    }

    @Override
    public void unregisterDataSetObserver(DataSetObserver observer) {
    }

    @Override
    public void setNotificationUri(android.content.ContentResolver cr, Uri uri) {
        mNotifyUri = uri;
    }

    @Override
    public Uri getNotificationUri() {
        return mNotifyUri;
    }

    @Override
    public List<Uri> getNotificationUris() {
        return null;
    }

    @Override
    public void setNotificationUris(android.content.ContentResolver cr, List<Uri> uris) {
    }

    @Override
    public boolean getWantsAllOnMoveCalls() {
        return false;
    }

    @Override
    public void setExtras(Bundle extras) {
        mExtras = extras;
    }

    @Override
    public Bundle getExtras() {
        return mExtras != null ? mExtras : new Bundle();
    }

    @Override
    public Bundle respond(Bundle extras) {
        return new Bundle();
    }
}
