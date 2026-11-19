package sun.reflect.generics.reflectiveObjects;

import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.util.Arrays;

/**
 * {@link ParameterizedType} 镜像实现（M 级最小 POJO）。
 * VM 接缝（core/builtins Class.getGenericSuperclass/getGenericInterfaces）按
 * dex Signature 属性解析后直接填字段构造;字段布局经链接器寻址,getter 纯 Java。
 */
public class ParameterizedTypeImpl implements ParameterizedType {

    private Type rawType;
    private Type[] actualTypeArguments;
    private Type ownerType;

    /** VM 直填字段（AllocObject 后布局写入）;代码路径仅 getter/语义面。 */
    public ParameterizedTypeImpl() {}

    @Override
    public Type[] getActualTypeArguments() {
        return actualTypeArguments.clone();
    }

    @Override
    public Type getRawType() {
        return rawType;
    }

    @Override
    public Type getOwnerType() {
        return ownerType;
    }

    @Override
    public String getTypeName() {
        return toString();
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        sb.append(((Class<?>) rawType).getName());
        if (actualTypeArguments != null && actualTypeArguments.length > 0) {
            sb.append('<');
            for (int i = 0; i < actualTypeArguments.length; i++) {
                if (i > 0) {
                    sb.append(", ");
                }
                sb.append(actualTypeArguments[i].getTypeName());
            }
            sb.append('>');
        }
        return sb.toString();
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof ParameterizedType)) {
            return false;
        }
        ParameterizedType that = (ParameterizedType) o;
        Type thatOwner = that.getOwnerType();
        Type thatRaw = that.getRawType();
        boolean ownerEq = (ownerType == null) ? thatOwner == null : ownerType.equals(thatOwner);
        boolean rawEq = (rawType == null) ? thatRaw == null : rawType.equals(thatRaw);
        return ownerEq && rawEq
                && Arrays.equals(actualTypeArguments, that.getActualTypeArguments());
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(actualTypeArguments)
                ^ (ownerType == null ? 0 : ownerType.hashCode())
                ^ (rawType == null ? 0 : rawType.hashCode());
    }
}
