package android.database;

/**
 * 数据库面异常基类（AOSP android.database.SQLException 公开面；§31.7 C 档 lib）。
 * RuntimeException 族；SQLite 族异常（SQLiteException 等）经本类下沉。
 */
public class SQLException extends RuntimeException {
    public SQLException() {
        super();
    }

    public SQLException(String error) {
        super(error);
    }
}
