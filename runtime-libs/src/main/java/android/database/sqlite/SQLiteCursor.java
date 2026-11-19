package android.database.sqlite;

import android.database.MatrixCursor;

/**
 * SQLite 查询游标（AOSP android.database.sqlite.SQLiteCursor 构造/公开面）。
 * 骨架形态：MatrixCursor 基座（AOSP 超链 AbstractWindowedCursor 缝后置补齐）；
 * 引擎缝落地后本类收窄为窗口游标薄包装。
 */
public class SQLiteCursor extends MatrixCursor {
    private final SQLiteCursorDriver mDriver;
    private final String mEditTable;
    private final SQLiteQuery mQuery;
    private boolean mFillWindowForwardOnly;

    public SQLiteCursor(SQLiteCursorDriver driver, String editTable, SQLiteQuery query) {
        super(new String[] {"_id"});
        mDriver = driver;
        mEditTable = editTable;
        mQuery = query;
    }

    public void setFillWindowForwardOnly(boolean fillWindowForwardOnly) {
        mFillWindowForwardOnly = fillWindowForwardOnly;
    }

    public SQLiteCursorDriver getDriver() {
        return mDriver;
    }

    public String getEditTable() {
        return mEditTable;
    }

    public SQLiteQuery getQuery() {
        return mQuery;
    }

    @Override
    public void close() {
        super.close();
        if (mDriver != null) {
            mDriver.cursorClosed();
        }
    }

    @Override
    public boolean requery() {
        if (mDriver != null) {
            mDriver.cursorRequeried(this);
        }
        return super.requery();
    }
}
