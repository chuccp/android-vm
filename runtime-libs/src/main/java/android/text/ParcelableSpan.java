package android.text;

/**
 * Spannable 序列化接口（AOSP android.text.ParcelableSpan 形状）。
 * 生成补源：lib 编译期需要，guest 侧 span 类型常见实现面。
 */
public interface ParcelableSpan extends android.os.Parcelable {
    int getSpanTypeId();
}
