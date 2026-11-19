package android.content;

import android.os.Bundle;
import android.os.Parcelable;

import java.io.Serializable;
import java.util.ArrayList;

/**
 * android.content.Intent 数据面（§31.7 B 档：POJO→lib——Intent 数据面/Bundle
 * 等纯数据载体一律 lib Java 源，VM 侧不留内建；framework 钩子只持有 Intent
 * 引用）。语义按 Android Intent 数据面：action/package/flags/component +
 * Bundle extras（putExtra 族/getXxxExtra 族），toString 为 Android 形态
 * （Intent { act=... pkg=... flg=... (has extras) }）。
 */
public class Intent implements Cloneable {

    private String mAction;
    private String mPackage;
    private String mComponentClass;
    private String mComponentPackage;
    private int mFlags;
    private Bundle mExtras;

    public Intent() {
    }

    public Intent(String action) {
        setAction(action);
    }

    /**
     * Intent(Context packageContext, Class&lt;?&gt; cls)：AOSP 显式组件构造
     * （component = {packageContext.getPackageName(), cls.getName()}）。
     */
    public Intent(Context packageContext, Class<?> cls) {
        if (packageContext != null && cls != null) {
            this.mComponentPackage = packageContext.getPackageName();
            this.mComponentClass = cls.getName();
        }
    }

    public Intent(Context packageContext, String className) {
        if (packageContext != null && className != null) {
            this.mComponentPackage = packageContext.getPackageName();
            this.mComponentClass = className;
        }
    }

    public Intent(Intent other) {
        if (other != null) {
            mAction = other.mAction;
            mPackage = other.mPackage;
            mComponentClass = other.mComponentClass;
            mComponentPackage = other.mComponentPackage;
            mFlags = other.mFlags;
            mExtras = other.mExtras == null ? null : new Bundle(other.mExtras);
        }
    }

    // ---- action/package/component/flags 面 ----

    public String getAction() {
        return mAction;
    }

    public Intent setAction(String action) {
        mAction = action;
        return this;
    }

    public String getPackage() {
        return mPackage;
    }

    public Intent setPackage(String packageName) {
        mPackage = packageName;
        return this;
    }

    public Intent setClassName(String packageContext, String className) {
        mComponentPackage = packageContext;
        mComponentClass = className;
        return this;
    }

    public Intent setClassName(Context packageContext, String className) {
        if (packageContext != null) {
            mComponentPackage = packageContext.getPackageName();
        }
        mComponentClass = className;
        return this;
    }

    public Intent setClass(Context packageContext, Class<?> cls) {
        if (packageContext != null) {
            mComponentPackage = packageContext.getPackageName();
        }
        mComponentClass = cls != null ? cls.getName() : null;
        return this;
    }

    public ComponentName getComponent() {
        if (mComponentPackage == null || mComponentClass == null) {
            return null;
        }
        return new ComponentName(mComponentPackage, mComponentClass);
    }

    public String getComponentClassName() {
        return mComponentClass;
    }

    public String getComponentPackage() {
        return mComponentPackage;
    }

    public int getFlags() {
        return mFlags;
    }

    public Intent setFlags(int flags) {
        mFlags = flags;
        return this;
    }

    public Intent addFlags(int flags) {
        mFlags |= flags;
        return this;
    }

    public Intent removeFlags(int flags) {
        mFlags &= ~flags;
        return this;
    }

    private ClassLoader mExtrasClassLoader;

    public void setExtrasClassLoader(ClassLoader loader) {
        this.mExtrasClassLoader = loader;
    }

    public ClassLoader getExtrasClassLoader() {
        return this.mExtrasClassLoader;
    }

    // ---- extras 面（Bundle 承载；key 缺省 = getXxxExtra 默认值语义）----

    public Bundle getExtras() {
        return mExtras;
    }

    public Intent putExtras(Bundle extras) {
        if (extras != null) {
            extras().putAll(extras);
        }
        return this;
    }

    public boolean hasExtra(String name) {
        return mExtras != null && mExtras.containsKey(name);
    }

    public void removeExtra(String name) {
        if (mExtras != null) {
            mExtras.remove(name);
        }
    }

    private Bundle extras() {
        if (mExtras == null) {
            mExtras = new Bundle();
        }
        return mExtras;
    }

    // ---- putExtra 族（Android Intent 数据面标量/String/Bundle 形）----

    public Intent putExtra(String name, String value) {
        extras().putString(name, value);
        return this;
    }

    public Intent putExtra(String name, CharSequence value) {
        extras().putCharSequence(name, value);
        return this;
    }

    public Intent putExtra(String name, boolean value) {
        extras().putBoolean(name, value);
        return this;
    }

    public Intent putExtra(String name, byte value) {
        extras().putByte(name, value);
        return this;
    }

    public Intent putExtra(String name, char value) {
        extras().putChar(name, value);
        return this;
    }

    public Intent putExtra(String name, short value) {
        extras().putShort(name, value);
        return this;
    }

    public Intent putExtra(String name, int value) {
        extras().putInt(name, value);
        return this;
    }

    public Intent putExtra(String name, long value) {
        extras().putLong(name, value);
        return this;
    }

    public Intent putExtra(String name, float value) {
        extras().putFloat(name, value);
        return this;
    }

    public Intent putExtra(String name, double value) {
        extras().putDouble(name, value);
        return this;
    }

    public Intent putExtra(String name, Bundle value) {
        extras().putBundle(name, value);
        return this;
    }

    public Intent putExtra(String name, Parcelable value) {
        extras().putParcelable(name, value);
        return this;
    }

    public Intent putExtra(String name, Serializable value) {
        extras().putSerializable(name, value);
        return this;
    }

    public Intent putParcelableArrayListExtra(String name, ArrayList<? extends Parcelable> value) {
        extras().putParcelableArrayList(name, value);
        return this;
    }

    public Intent putStringArrayListExtra(String name, ArrayList<String> value) {
        extras().putStringArrayList(name, value);
        return this;
    }

    public Intent putIntegerArrayListExtra(String name, ArrayList<Integer> value) {
        extras().putIntegerArrayList(name, value);
        return this;
    }

    public Intent putExtra(String name, String[] value) {
        extras().putStringArray(name, value);
        return this;
    }

    public Intent putExtra(String name, int[] value) {
        extras().putIntArray(name, value);
        return this;
    }

    public Intent putExtra(String name, long[] value) {
        extras().putLongArray(name, value);
        return this;
    }

    // ---- getXxxExtra 族（key 缺省返回默认值；类型错配按 Android 口径抛
    //      ClassCastException 由 Bundle get 面承接）----

    public String getStringExtra(String name) {
        return mExtras == null ? null : mExtras.getString(name);
    }

    public CharSequence getCharSequenceExtra(String name) {
        return mExtras == null ? null : mExtras.getCharSequence(name);
    }

    public boolean getBooleanExtra(String name, boolean defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getBoolean(name, defaultValue);
    }

    public byte getByteExtra(String name, byte defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getByte(name, defaultValue);
    }

    public char getCharExtra(String name, char defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getChar(name, defaultValue);
    }

    public short getShortExtra(String name, short defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getShort(name, defaultValue);
    }

    public int getIntExtra(String name, int defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getInt(name, defaultValue);
    }

    public long getLongExtra(String name, long defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getLong(name, defaultValue);
    }

    public float getFloatExtra(String name, float defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getFloat(name, defaultValue);
    }

    public double getDoubleExtra(String name, double defaultValue) {
        return mExtras == null ? defaultValue : mExtras.getDouble(name, defaultValue);
    }

    public Bundle getBundleExtra(String name) {
        return mExtras == null ? null : mExtras.getBundle(name);
    }

    public Parcelable getParcelableExtra(String name) {
        return mExtras == null ? null : mExtras.getParcelable(name);
    }

    public <T extends Parcelable> T getParcelableExtra(String name, Class<T> clazz) {
        Parcelable value = getParcelableExtra(name);
        return clazz.isInstance(value) ? clazz.cast(value) : null;
    }

    public ArrayList<Parcelable> getParcelableArrayListExtra(String name) {
        return mExtras == null ? null : mExtras.getParcelableArrayList(name);
    }

    public <T extends Parcelable> ArrayList<T> getParcelableArrayListExtra(String name,
                                                                          Class<T> clazz) {
        return mExtras == null ? null : mExtras.getParcelableArrayList(name);
    }

    public Serializable getSerializableExtra(String name) {
        return mExtras == null ? null : mExtras.getSerializable(name);
    }

    public <T extends Serializable> T getSerializableExtra(String name, Class<T> clazz) {
        Serializable value = getSerializableExtra(name);
        return clazz.isInstance(value) ? clazz.cast(value) : null;
    }

    public ArrayList<String> getStringArrayListExtra(String name) {
        return mExtras == null ? null : mExtras.getStringArrayList(name);
    }

    public ArrayList<Integer> getIntegerArrayListExtra(String name) {
        return mExtras == null ? null : mExtras.getIntegerArrayList(name);
    }

    public String[] getStringArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getStringArray(name);
    }

    public int[] getIntArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getIntArray(name);
    }

    public long[] getLongArrayExtra(String name) {
        return mExtras == null ? null : mExtras.getLongArray(name);
    }

    @Override
    public String toString() {
        StringBuilder b = new StringBuilder("Intent {");
        if (mAction != null) b.append(" act=").append(mAction);
        if (mPackage != null) b.append(" pkg=").append(mPackage);
        if (mComponentClass != null) {
            b.append(" cmp=").append(mComponentPackage).append('/').append(mComponentClass);
        }
        if (mFlags != 0) b.append(" flg=0x").append(Integer.toHexString(mFlags));
        if (mExtras != null) b.append(" (has extras)");
        b.append(" }");
        return b.toString();
    }
}
