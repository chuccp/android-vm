package android.database.sqlite;

import android.database.SQLException;

/**
 * SQLite 异常基类（AOSP android.database.sqlite.SQLiteException 公开面）。
 * 族下具体异常（Full/BlobTooBig/Corrupt/Locked 等）按链上暴露逐个补。
 */
public class SQLiteException extends SQLException {
    public SQLiteException() {
        super();
    }

    public SQLiteException(String error) {
        super(error);
    }
}
