package android.database;

/** AOSP DataSetObserver（数据集变更/失效回调；抽象类面）。 */
public abstract class DataSetObserver {
    public void onChanged() {
    }

    public void onInvalidated() {
    }
}
