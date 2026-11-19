package android.database;

import android.net.Uri;
import android.os.Bundle;

import java.util.ArrayList;
import java.util.List;

/**
 * Cursor 基类（AOSP android.database.AbstractCursor 公开面；§31.7 C 档 lib）。
 * 位置态（mPos）+ 列数/列名抽象 + 观察者/通知面（记录式：无 ContentService
 * 泵不产生回调，AOSP 无监听者同效）。具体行存取由子类（MatrixCursor/SQLiteCursor）
 * 承担；未覆写列访问 = AbstractMethodError 语义（抽象面）。
 */
public abstract class AbstractCursor implements Cursor {
    protected int mPos = -1;
    protected boolean mClosed = false;
    private Uri mNotifyUri;
    private Bundle mExtras = new Bundle();
    private final List<ContentObserver> mContentObservers = new ArrayList<ContentObserver>();
    private final List<DataSetObserver> mDataSetObservers = new ArrayList<DataSetObserver>();

    @Override
    public int getCount() {
        throw new UnsupportedOperationException("getCount not implemented");
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
        final int count = getCount();
        if (position >= count) {
            mPos = count;
            return false;
        }
        if (position < 0) {
            mPos = -1;
            return false;
        }
        mPos = position;
        return true;
    }

    @Override
    public boolean moveToFirst() {
        return moveToPosition(0);
    }

    @Override
    public boolean moveToLast() {
        return moveToPosition(getCount() - 1);
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
        return mPos == 0 && getCount() != 0;
    }

    @Override
    public boolean isLast() {
        int cnt = getCount();
        return cnt != 0 && mPos == cnt - 1;
    }

    @Override
    public boolean isBeforeFirst() {
        return getCount() == 0 || mPos == -1;
    }

    @Override
    public boolean isAfterLast() {
        return getCount() == 0 || mPos == getCount();
    }

    @Override
    public int getColumnIndex(String columnName) {
        String[] names = getColumnNames();
        for (int i = 0; i < names.length; i++) {
            if (names[i].equals(columnName)) {
                return i;
            }
        }
        // AOSP：尾缀模糊匹配（".name" 可命中 "table.name"）
        for (int i = 0; i < names.length; i++) {
            if (names[i].endsWith("." + columnName)) {
                return i;
            }
        }
        return -1;
    }

    @Override
    public int getColumnIndexOrThrow(String columnName) {
        int index = getColumnIndex(columnName);
        if (index < 0) {
            throw new IllegalArgumentException("column '" + columnName + "' does not exist");
        }
        return index;
    }

    @Override
    public String getColumnName(int columnIndex) {
        return getColumnNames()[columnIndex];
    }

    @Override
    public int getColumnCount() {
        return getColumnNames().length;
    }

    @Override
    public void copyStringToBuffer(int columnIndex, CharArrayBuffer buffer) {
        String s = getString(columnIndex);
        if (s == null) {
            s = "";
        }
        int len = s.length();
        if (buffer.data == null || buffer.data.length < len) {
            buffer.data = new char[len];
        }
        s.getChars(0, len, buffer.data, 0);
        buffer.sizeCopied = len;
    }

    @Override
    public void deactivate() {
    }

    @Override
    public boolean requery() {
        notifyDataSetObservers();
        return true;
    }

    @Override
    public boolean isClosed() {
        return mClosed;
    }

    @Override
    public void close() {
        mClosed = true;
        notifyDataSetObservers();
    }

    // ---- 观察者/通知面（记录式空操作：无变更泵）----

    @Override
    public void registerContentObserver(ContentObserver observer) {
        mContentObservers.add(observer);
    }

    @Override
    public void unregisterContentObserver(ContentObserver observer) {
        mContentObservers.remove(observer);
    }

    @Override
    public void registerDataSetObserver(DataSetObserver observer) {
        mDataSetObservers.add(observer);
    }

    @Override
    public void unregisterDataSetObserver(DataSetObserver observer) {
        mDataSetObservers.remove(observer);
    }

    private void notifyDataSetObservers() {
        for (int i = 0; i < mDataSetObservers.size(); i++) {
            mDataSetObservers.get(i).onChanged();
        }
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
        return mNotifyUri != null ? java.util.Collections.singletonList(mNotifyUri) : null;
    }

    @Override
    public void setNotificationUris(android.content.ContentResolver cr, List<Uri> uris) {
        mNotifyUri = (uris != null && !uris.isEmpty()) ? uris.get(0) : null;
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

    // ---- 列访问抽象面（子类承担）----

    @Override
    public abstract String getString(int columnIndex);

    @Override
    public abstract short getShort(int columnIndex);

    @Override
    public abstract int getInt(int columnIndex);

    @Override
    public abstract long getLong(int columnIndex);

    @Override
    public abstract float getFloat(int columnIndex);

    @Override
    public abstract double getDouble(int columnIndex);

    @Override
    public abstract byte[] getBlob(int columnIndex);

    @Override
    public abstract boolean isNull(int columnIndex);
}
