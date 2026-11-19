package sun.reflect.generics.reflectiveObjects;

import java.lang.reflect.GenericArrayType;
import java.lang.reflect.Type;

/**
 * {@link GenericArrayType} 镜像实现（M 级最小 POJO）：分量含类型变量/通配/参数化
 * 的泛型数组（如 T[]）。普通数组（int[]、String[]）直接以数组 Class 表达。
 * VM 接缝填字段构造。
 */
public class GenericArrayTypeImpl implements GenericArrayType {

    private Type genericComponentType;

    /** VM 直填字段（AllocObject 后布局写入）。 */
    public GenericArrayTypeImpl() {}

    @Override
    public Type getGenericComponentType() {
        return genericComponentType;
    }

    @Override
    public String getTypeName() {
        return toString();
    }

    @Override
    public String toString() {
        return genericComponentType.getTypeName() + "[]";
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof GenericArrayType)) {
            return false;
        }
        Type that = ((GenericArrayType) o).getGenericComponentType();
        return (genericComponentType == null) ? that == null : genericComponentType.equals(that);
    }

    @Override
    public int hashCode() {
        return genericComponentType == null ? 0 : genericComponentType.hashCode();
    }
}
