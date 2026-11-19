package android.os;

/**
 * 电量来源（AOSP android.os.WorkSource；§31.7 C 档 lib 骨架）。
 */
public class WorkSource {

    public WorkSource() {
    }

    public WorkSource(WorkSource orig) {
    }

    public boolean add(int uid) {
        return true;
    }

    public void clear() {
    }

    public int size() {
        return 0;
    }

    public int get(int index) {
        return -1;
    }

    @Override
    public boolean equals(Object o) {
        return this == o;
    }

    @Override
    public int hashCode() {
        return System.identityHashCode(this);
    }
}
