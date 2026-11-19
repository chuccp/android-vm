package android.content;

import android.net.Uri;
import android.os.Parcel;
import android.os.Parcelable;
import android.text.TextUtils;

/**
 * 剪贴数据（AOSP android.content.ClipData 公开面；§31.7 C 档 lib POJO——条目表）。
 */
public class ClipData implements Parcelable {

    /** 条目（AOSP ClipData.Item）。 */
    public static class Item {
        final CharSequence mText;
        final Intent mIntent;
        final Uri mUri;

        public Item(CharSequence text) {
            this.mText = text;
            this.mIntent = null;
            this.mUri = null;
        }

        public Item(Intent intent) {
            this.mText = null;
            this.mIntent = intent;
            this.mUri = null;
        }

        public Item(Uri uri) {
            this.mText = null;
            this.mIntent = null;
            this.mUri = uri;
        }

        public Item(CharSequence text, Intent intent, Uri uri) {
            this.mText = text;
            this.mIntent = intent;
            this.mUri = uri;
        }

        public CharSequence getText() {
            return this.mText;
        }

        public Intent getIntent() {
            return this.mIntent;
        }

        public Uri getUri() {
            return this.mUri;
        }

        public String coerceToText(Context context) {
            return this.mText != null ? this.mText.toString() : null;
        }
    }

    final CharSequence mLabel;
    final String[] mHtmlTypes;
    final String[] mTextTypes;
    final java.util.ArrayList<Item> mItems = new java.util.ArrayList<Item>();

    public ClipData(CharSequence label, String[] mimeTypes, Item item) {
        this.mLabel = label != null ? label : "";
        this.mHtmlTypes = null;
        this.mTextTypes = mimeTypes;
        if (item != null) this.mItems.add(item);
    }

    public ClipData(CharSequence label, String[] mimeTypes, Intent intent) {
        this(label, mimeTypes, new Item(intent));
    }

    public ClipData(ClipData other) {
        this.mLabel = other.mLabel;
        this.mHtmlTypes = null;
        this.mTextTypes = other.mTextTypes;
        this.mItems.addAll(other.mItems);
    }

    public static ClipData newPlainText(CharSequence label, CharSequence text) {
        return new ClipData(label, new String[] {"text/plain"}, new Item(text));
    }

    public static ClipData newRawUri(CharSequence label, Uri uri) {
        return new ClipData(label, new String[] {"text/uri-list"}, new Item(uri));
    }

    public static ClipData newIntent(CharSequence label, Intent intent) {
        return new ClipData(label, new String[] {"text/vnd.android.intent"}, new Item(intent));
    }

    public static ClipData newUri(ContentResolver resolver, CharSequence label, Uri uri) {
        return new ClipData(label, new String[] {"text/uri-list"}, new Item(uri));
    }

    public CharSequence getLabel() {
        return this.mLabel;
    }

    public int getItemCount() {
        return this.mItems.size();
    }

    public Item getItemAt(int index) {
        return this.mItems.get(index);
    }

    public void addItem(Item item) {
        this.mItems.add(item);
    }

    public void addItem(ContentResolver resolver, Item item) {
        this.mItems.add(item);
    }

    public String getDescription() {
        return this.mLabel != null ? this.mLabel.toString() : null;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }
}
