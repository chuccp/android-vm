package android.database;

import android.database.sqlite.SQLiteDatabase;

/**
 * 数据库损坏回调（AOSP android.database.DatabaseErrorHandler 公开面）。
 * SQLiteOpenHelper 5 号构造携带；骨架下仅 onCorruption 一个回调。
 */
public interface DatabaseErrorHandler {
    void onCorruption(SQLiteDatabase dbObj);
}
