package android.database.sqlite;

/**
 * 查询语句载体（AOSP android.database.sqlite.SQLiteQuery 面；SQLiteCursor
 * 构造携带的查询描述——骨架：绑定面继承 SQLiteProgram）。
 */
public class SQLiteQuery extends SQLiteProgram {
    SQLiteQuery(SQLiteDatabase db, String sql) {
        super(db, sql);
    }
}
