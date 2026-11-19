package android.database.sqlite;

import android.content.ContentValues;
import android.database.CharArrayBuffer;
import android.database.Cursor;
import android.database.DatabaseErrorHandler;
import android.database.MatrixCursor;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * SQLite 数据库门面（AOSP android.database.sqlite.SQLiteDatabase 公开面）。
 *
 * <p>§31.7 骨架判定（可执行不崩、语义合理；状态存字段）：SQL 引擎缝
 * （sqlite3 native）后置——本门面保持对象生命周期语义（open/close/version/
 * 事务嵌套/语句记录），SQL 文本进语句记录（mStatements，诊断面）不执行；
 * query/rawQuery 返回空 MatrixCursor（无表态的真实口径）；insert/update/
 * delete 计 1 行（成功形计数，非引擎行 id）。真实引擎缝（jni + sqlite3）
 * 落地后本类收窄为薄包装（AOSP 形）。
 */
public class SQLiteDatabase {
    /** 游标工厂（AOSP SQLiteDatabase.CursorFactory 公开接口面）。 */
    public interface CursorFactory {
        Cursor newCursor(SQLiteDatabase db, SQLiteCursorDriver masterQuery,
                String editTable, SQLiteQuery query);
    }

    public static final int CONFLICT_NONE = 0;
    public static final int CONFLICT_ROLLBACK = 1;
    public static final int CONFLICT_ABORT = 2;
    public static final int CONFLICT_FAIL = 3;
    public static final int CONFLICT_IGNORE = 4;
    public static final int CONFLICT_REPLACE = 5;

    private final String mPath;
    private final CursorFactory mFactory;
    private boolean mOpen = true;
    private int mVersion;
    private int mTransactionDepth;
    private boolean mTransactionSuccessful;
    private boolean mWriteAheadLogging;
    private final List<String> mStatements = new ArrayList<String>();
    private final List<Object> mAttachedDbs = new ArrayList<Object>();

    private SQLiteDatabase(String path, CursorFactory factory) {
        mPath = path;
        mFactory = factory;
    }

    // ---- 打开/关闭面 ----

    public static SQLiteDatabase openDatabase(String path, CursorFactory factory, int flags) {
        return new SQLiteDatabase(path, factory);
    }

    public static SQLiteDatabase openDatabase(String path, CursorFactory factory, int flags,
            DatabaseErrorHandler errorHandler) {
        return new SQLiteDatabase(path, factory);
    }

    public static SQLiteDatabase openOrCreateDatabase(File file, CursorFactory factory) {
        return new SQLiteDatabase(file != null ? file.getPath() : ":memory:", factory);
    }

    public static SQLiteDatabase openOrCreateDatabase(File file, CursorFactory factory,
            DatabaseErrorHandler errorHandler) {
        return openOrCreateDatabase(file, factory);
    }

    public static SQLiteDatabase openOrCreateDatabase(String path, CursorFactory factory) {
        return new SQLiteDatabase(path, factory);
    }

    public static boolean deleteDatabase(File file) {
        if (file == null) {
            return false;
        }
        boolean ok = file.delete();
        File journal = new File(file.getPath() + "-journal");
        journal.delete();
        File wal = new File(file.getPath() + "-wal");
        wal.delete();
        File shm = new File(file.getPath() + "-shm");
        shm.delete();
        return ok || !file.exists();
    }

    public void close() {
        mOpen = false;
    }

    public boolean isOpen() {
        return mOpen;
    }

    public String getPath() {
        return mPath;
    }

    public int getVersion() {
        return mVersion;
    }

    public void setVersion(int version) {
        mVersion = version;
    }

    // ---- 事务面 ----

    public void beginTransaction() {
        mTransactionDepth++;
        mTransactionSuccessful = false;
    }

    public void beginTransactionNonExclusive() {
        beginTransaction();
    }

    public void setTransactionSuccessful() {
        if (mTransactionDepth == 0) {
            throw new IllegalStateException("no transaction pending");
        }
        mTransactionSuccessful = true;
    }

    public void endTransaction() {
        if (mTransactionDepth == 0) {
            throw new IllegalStateException("no transaction pending");
        }
        mTransactionDepth--;
        mTransactionSuccessful = false;
    }

    public boolean inTransaction() {
        return mTransactionDepth > 0;
    }

    // ---- SQL 执行面（骨架：语句记录不执行；引擎缝后置）----

    public void execSQL(String sql) throws SQLiteException {
        mStatements.add(sql);
    }

    public void execSQL(String sql, Object[] bindArgs) throws SQLiteException {
        mStatements.add(sql);
    }

    /** 语句记录（诊断面：骨架下 execSQL 的 SQL 文本，出序即记录序）。 */
    public List<String> getExecutedStatements() {
        return new ArrayList<String>(mStatements);
    }

    public SQLiteStatement compileStatement(String sql) throws SQLiteException {
        mStatements.add(sql);
        return new SQLiteStatement(this, sql);
    }

    // ---- 查询面（骨架：无表态空游标；AOSP 空表同口径）----

    public Cursor query(String table, String[] columns, String selection,
            String[] selectionArgs, String groupBy, String having, String orderBy) {
        return emptyCursor(columns);
    }

    public Cursor query(String table, String[] columns, String selection,
            String[] selectionArgs, String groupBy, String having, String orderBy, String limit) {
        return emptyCursor(columns);
    }

    public Cursor query(boolean distinct, String table, String[] columns, String selection,
            String[] selectionArgs, String groupBy, String having, String orderBy, String limit) {
        return emptyCursor(columns);
    }

    public Cursor rawQuery(String sql, String[] selectionArgs) {
        mStatements.add(sql);
        return emptyCursor(null);
    }

    public Cursor rawQueryWithFactory(CursorFactory factory, String sql, String[] selectionArgs,
            String editTable) {
        mStatements.add(sql);
        return emptyCursor(null);
    }

    public Cursor rawQueryWithFactory(CursorFactory factory, String sql, String[] selectionArgs,
            String editTable, android.os.CancellationSignal cancellationSignal) {
        mStatements.add(sql);
        return emptyCursor(null);
    }

    private static Cursor emptyCursor(String[] columns) {
        String[] cols = (columns == null || columns.length == 0)
                ? new String[] {"_id"} : columns;
        return new MatrixCursor(cols);
    }

    // ---- 写面（骨架：计 1 行；非引擎行 id）----

    public long insert(String table, String nullColumnHack, ContentValues values) {
        return insertWithOnConflict(table, nullColumnHack, values, CONFLICT_NONE);
    }

    public long insertOrThrow(String table, String nullColumnHack, ContentValues values)
            throws SQLiteException {
        return insertWithOnConflict(table, nullColumnHack, values, CONFLICT_NONE);
    }

    public long replace(String table, String nullColumnHack, ContentValues initialValues) {
        return insertWithOnConflict(table, nullColumnHack, initialValues, CONFLICT_REPLACE);
    }

    public long replaceOrThrow(String table, String nullColumnHack, ContentValues initialValues) {
        return insertWithOnConflict(table, nullColumnHack, initialValues, CONFLICT_REPLACE);
    }

    public long insertWithOnConflict(String table, String nullColumnHack, ContentValues values,
            int conflictAlgorithm) {
        mStatements.add("INSERT " + table);
        return 1L;
    }

    public int update(String table, ContentValues values, String whereClause,
            String[] whereArgs) {
        return updateWithOnConflict(table, values, whereClause, whereArgs, CONFLICT_NONE);
    }

    public int updateWithOnConflict(String table, ContentValues values, String whereClause,
            String[] whereArgs, int conflictAlgorithm) {
        mStatements.add("UPDATE " + table);
        return 1;
    }

    public int delete(String table, String whereClause, String[] whereArgs) {
        mStatements.add("DELETE " + table);
        return 1;
    }

    // ---- 其余公开面 ----

    public List<Object> getAttachedDbs() {
        return mAttachedDbs.isEmpty() ? null : new ArrayList<Object>(mAttachedDbs);
    }

    public boolean enableWriteAheadLogging() {
        mWriteAheadLogging = true;
        return true;
    }

    public boolean disableWriteAheadLogging() {
        mWriteAheadLogging = false;
        return true;
    }

    public boolean isWriteAheadLoggingEnabled() {
        return mWriteAheadLogging;
    }

    public void setMaxSqlCacheSize(int cacheSize) {
    }

    public boolean needUpgrade(int newVersion) {
        return mVersion < newVersion;
    }

    public static boolean isDatabaseIntegrityOk(SQLiteDatabase db) {
        return true;
    }
}
