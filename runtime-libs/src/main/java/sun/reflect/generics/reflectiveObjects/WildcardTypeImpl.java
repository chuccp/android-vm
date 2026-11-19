package sun.reflect.generics.reflectiveObjects;

import java.lang.reflect.Type;
import java.lang.reflect.WildcardType;
import java.util.Arrays;

/**
 * {@link WildcardType} 镜像实现（M 级最小 POJO）。
 * 形态：{@code ?}（upper=[Object]）、{@code ? extends X}（upper=[X]）、
 * {@code ? super Y}（lower=[Y]、upper=[Object]）。VM 接缝填字段构造。
 */
public class WildcardTypeImpl implements WildcardType {

    private Type[] upperBounds;
    private Type[] lowerBounds;

    /** VM 直填字段（AllocObject 后布局写入）。 */
    public WildcardTypeImpl() {}

    @Override
    public Type[] getUpperBounds() {
        return upperBounds.clone();
    }

    @Override
    public Type[] getLowerBounds() {
        return lowerBounds.clone();
    }

    @Override
    public String getTypeName() {
        return toString();
    }

    @Override
    public String toString() {
        if (lowerBounds != null && lowerBounds.length > 0) {
            return "? super " + lowerBounds[0].getTypeName();
        }
        if (upperBounds != null && upperBounds.length > 0 && upperBounds[0] != Object.class) {
            return "? extends " + upperBounds[0].getTypeName();
        }
        return "?";
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof WildcardType)) {
            return false;
        }
        WildcardType that = (WildcardType) o;
        return Arrays.equals(upperBounds, that.getUpperBounds())
                && Arrays.equals(lowerBounds, that.getLowerBounds());
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(upperBounds) ^ Arrays.hashCode(lowerBounds);
    }
}
