package android.database;

/** AOSP CharArrayBuffer（Cursor.copyStringToBuffer 目标缓冲；公开字段面）。 */
public class CharArrayBuffer {
    public char[] data;
    public int sizeCopied;

    public CharArrayBuffer(int size) {
        data = new char[size];
    }

    public CharArrayBuffer(char[] buf) {
        data = buf;
    }
}
