package android.content;

import android.os.Parcel;
import android.os.Parcelable;
import java.util.Objects;

public final class ComponentName implements Parcelable, Cloneable {
    public final String mPackage;
    public final String mClass;

    public ComponentName(String pkg, String cls) {
        mPackage = Objects.requireNonNull(pkg);
        mClass = Objects.requireNonNull(cls);
    }

    public ComponentName(Context pkg, String cls) {
        this(pkg.getPackageName(), cls);
    }

    public ComponentName(Context pkg, Class<?> cls) {
        this(pkg.getPackageName(), cls.getName());
    }

    public ComponentName(ComponentName source) {
        mPackage = source.mPackage;
        mClass = source.mClass;
    }

    public String getPackageName() {
        return mPackage;
    }

    public String getClassName() {
        return mClass;
    }

    public String getShortClassName() {
        String prefix = mPackage + ".";
        return mClass.startsWith(prefix)
                ? mClass.substring(prefix.length())
                : mClass;
    }

    public String flattenToString() {
        return mPackage + "/" + mClass;
    }

    public String flattenToShortString() {
        return mPackage + "/" + getShortClassName();
    }

    public String toShortString() {
        return "{" + flattenToShortString() + "}";
    }

    public static ComponentName unflattenFromString(String value) {
        if (value == null) return null;
        int separator = value.indexOf('/');
        if (separator <= 0 || separator >= value.length() - 1) return null;
        return new ComponentName(
                value.substring(0, separator),
                value.substring(separator + 1));
    }

    public void appendShortString(StringBuilder builder) {
        builder.append(mPackage).append('/').append(getShortClassName());
    }

    @Override
    public ComponentName clone() {
        return new ComponentName(this);
    }

    @Override
    public boolean equals(Object other) {
        if (!(other instanceof ComponentName)) return false;
        ComponentName that = (ComponentName) other;
        return mPackage.equals(that.mPackage) && mClass.equals(that.mClass);
    }

    @Override
    public int hashCode() {
        return Objects.hash(mPackage, mClass);
    }

    @Override
    public String toString() {
        return "ComponentInfo{" + mPackage + "/" + mClass + "}";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
        out.writeString(mPackage);
        out.writeString(mClass);
    }

    public void readFromParcel(Parcel in) {
        throw new UnsupportedOperationException(
                "ComponentName fields are immutable");
    }

    public static final Parcelable.Creator<ComponentName> CREATOR =
            new Parcelable.Creator<ComponentName>() {
                @Override
                public ComponentName createFromParcel(Parcel in) {
                    return new ComponentName(
                            in.readString(),
                            in.readString());
                }

                @Override
                public ComponentName[] newArray(int size) {
                    return new ComponentName[size];
                }
            };
}
