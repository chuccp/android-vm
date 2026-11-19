package android.database.sqlite;

import android.database.Cursor;

/**
 * 游标驱动接口（AOSP android.database.sqlite.SQLiteCursorDriver 公开面）。
 * 窗口游标再查询/关闭回调（引擎缝消费；骨架面全形登记）。
 */
public interface SQLiteCursorDriver {
    void setTable(String table);

    Cursor query(SQLiteDatabase.CursorFactory factory);

    void cursorRequeried(Cursor cursor);

    void cursorClosed();
}
