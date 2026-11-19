package android.database.sqlite;

/** AOSP SQLiteFullException（磁盘满；族形 lib 骨架）。 */
public class SQLiteFullException extends SQLiteException {
    public SQLiteFullException() {
        super();
    }

    public SQLiteFullException(String error) {
        super(error);
    }

    @Override
    public String toString() {
        return "SQLiteFullException: " + getMessage();
    }
}
