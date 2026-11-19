package android.database.sqlite;

/** AOSP SQLiteDatabaseLockedException（族形 lib 骨架）。 */
public class SQLiteDatabaseLockedException extends SQLiteException {
    public SQLiteDatabaseLockedException() {
        super();
    }

    public SQLiteDatabaseLockedException(String error) {
        super(error);
    }
}
