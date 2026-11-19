package android.database.sqlite;

import android.content.Context;
import android.database.DatabaseErrorHandler;

/**
 * 数据库帮助器（AOSP android.database.sqlite.SQLiteOpenHelper 公开面）。
 * 语义：per-实例缓存 SQLiteDatabase；首开（version 0）走 onCreate 并落
 * newVersion，版本差走 onUpgrade/onDowngrade；onConfigure/onOpen 前后置回调。
 * 骨架下底层 = SQLiteDatabase 门面（引擎缝后置）。
 */
public abstract class SQLiteOpenHelper {
    private final Context mContext;
    private final String mName;
    private final SQLiteDatabase.CursorFactory mFactory;
    private final int mNewVersion;
    private final DatabaseErrorHandler mErrorHandler;
    private SQLiteDatabase mDatabase;
    private boolean mClosed;

    public SQLiteOpenHelper(Context context, String name, SQLiteDatabase.CursorFactory factory,
            int version) {
        this(context, name, factory, version, null);
    }

    public SQLiteOpenHelper(Context context, String name, SQLiteDatabase.CursorFactory factory,
            int version, DatabaseErrorHandler errorHandler) {
        if (version < 1) {
            throw new IllegalArgumentException("Version must be >= 1, was " + version);
        }
        mContext = context;
        mName = name;
        mFactory = factory;
        mNewVersion = version;
        mErrorHandler = errorHandler;
    }

    public synchronized SQLiteDatabase getWritableDatabase() {
        return getDatabaseLocked(true);
    }

    public synchronized SQLiteDatabase getReadableDatabase() {
        return getDatabaseLocked(false);
    }

    private SQLiteDatabase getDatabaseLocked(boolean writable) {
        if (mDatabase != null && mDatabase.isOpen()) {
            return mDatabase;
        }
        SQLiteDatabase db;
        if (mName == null) {
            db = SQLiteDatabase.openOrCreateDatabase((String) null, mFactory);
        } else {
            db = SQLiteDatabase.openOrCreateDatabase(mName, mFactory);
        }
        onConfigure(db);
        int version = db.getVersion();
        if (version != mNewVersion) {
            if (version == 0) {
                onCreate(db);
            } else if (version > mNewVersion) {
                onDowngrade(db, version, mNewVersion);
            } else {
                onUpgrade(db, version, mNewVersion);
            }
            db.setVersion(mNewVersion);
        }
        onOpen(db);
        mDatabase = db;
        mClosed = false;
        return db;
    }

    public synchronized void close() {
        mClosed = true;
        if (mDatabase != null && mDatabase.isOpen()) {
            mDatabase.close();
        }
        mDatabase = null;
    }

    public String getDatabaseName() {
        return mName;
    }

    public void setWriteAheadLoggingEnabled(boolean enabled) {
        if (mDatabase != null) {
            if (enabled) {
                mDatabase.enableWriteAheadLogging();
            } else {
                mDatabase.disableWriteAheadLogging();
            }
        }
    }

    public void onConfigure(SQLiteDatabase db) {
    }

    public abstract void onCreate(SQLiteDatabase db);

    public abstract void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion);

    public void onDowngrade(SQLiteDatabase db, int oldVersion, int newVersion) {
        throw new SQLiteException("Can't downgrade database from version " + oldVersion + " to "
                + newVersion);
    }

    public void onOpen(SQLiteDatabase db) {
    }
}
