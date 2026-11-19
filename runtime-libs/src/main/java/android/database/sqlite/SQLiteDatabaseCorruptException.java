package android.database.sqlite;

/** AOSP SQLiteDatabaseCorruptException（族形 lib 骨架）。 */
public class SQLiteDatabaseCorruptException extends SQLiteException {
    public SQLiteDatabaseCorruptException() {
        super();
    }

    public SQLiteDatabaseCorruptException(String error) {
        super(error);
    }
}
