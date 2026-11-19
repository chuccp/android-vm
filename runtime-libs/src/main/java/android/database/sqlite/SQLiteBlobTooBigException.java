package android.database.sqlite;

/** AOSP SQLiteBlobTooBigException（族形 lib 骨架）。 */
public class SQLiteBlobTooBigException extends SQLiteException {
    public SQLiteBlobTooBigException() {
        super();
    }

    public SQLiteBlobTooBigException(String error) {
        super(error);
    }
}
