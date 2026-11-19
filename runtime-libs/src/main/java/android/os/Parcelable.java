package android.os;

/**
 * 可打包对象接口（AOSP android.os.Parcelable 公开面；§31.7 C 档 lib）。
 * Creator/ClassLoaderCreator 族同形（泛型擦除）。
 */
public interface Parcelable {

    int CONTENTS_FILE_DESCRIPTOR = 0x0001;
    int PARCELABLE_WRITE_RETURN_VALUE = 0x0001;

    int describeContents();

    void writeToParcel(Parcel dest, int flags);

    interface Creator<T> {
        T createFromParcel(Parcel source);

        T[] newArray(int size);
    }

    interface ClassLoaderCreator<T> extends Creator<T> {
        T createFromParcel(Parcel source, ClassLoader loader);
    }
}
