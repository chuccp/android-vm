package android.database.sqlite;

/**
 * SQL 语句绑定基类（AOSP android.database.sqlite.SQLiteProgram 公开面）。
 * 绑定槽记录（骨架：引擎缝后置；execute 面由子类承担）。
 */
public abstract class SQLiteProgram {
    private final SQLiteDatabase mDatabase;
    private final String mSql;
    private final Object[] mBindArgs;
    private boolean mClosed;

    SQLiteProgram(SQLiteDatabase db, String sql) {
        mDatabase = db;
        mSql = sql;
        mBindArgs = new Object[64];
    }

    public final SQLiteDatabase getDatabase() {
        return mDatabase;
    }

    public final String getSql() {
        return mSql;
    }

    public final void bindNull(int index) {
        mBindArgs[index] = null;
    }

    public final void bindLong(int index, long value) {
        mBindArgs[index] = value;
    }

    public final void bindDouble(int index, double value) {
        mBindArgs[index] = value;
    }

    public final void bindString(int index, String value) {
        mBindArgs[index] = value;
    }

    public final void bindBlob(int index, byte[] value) {
        mBindArgs[index] = value;
    }

    public void bindAllArgsAsStrings(String[] bindArgs) {
        if (bindArgs != null) {
            for (int i = bindArgs.length; i != 0; i--) {
                bindString(i, bindArgs[i - 1]);
            }
        }
    }

    public void close() {
        mClosed = true;
    }

    public boolean isClosed() {
        return mClosed;
    }
}
