package android.database.sqlite;

/**
 * 可执行 SQL 语句（AOSP android.database.sqlite.SQLiteStatement 公开面）。
 * 骨架：execute* 记语句计成功（引擎缝后置收窄）。
 */
public class SQLiteStatement extends SQLiteProgram {
    SQLiteStatement(SQLiteDatabase db, String sql) {
        super(db, sql);
    }

    public void execute() {
        getDatabase().execSQL(getSql());
    }

    public long executeInsert() {
        getDatabase().execSQL(getSql());
        return 1L;
    }

    public int executeUpdateDelete() {
        getDatabase().execSQL(getSql());
        return 1;
    }

    public long simpleQueryForLong() {
        return 0L;
    }

    public String simpleQueryForString() {
        return null;
    }
}
