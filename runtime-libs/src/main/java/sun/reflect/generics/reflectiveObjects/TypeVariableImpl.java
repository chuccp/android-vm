package sun.reflect.generics.reflectiveObjects;

import java.lang.annotation.Annotation;
import java.lang.reflect.AnnotatedType;
import java.lang.reflect.GenericDeclaration;
import java.lang.reflect.Type;
import java.lang.reflect.TypeVariable;

/**
 * {@link TypeVariable} 镜像实现（M 级最小 POJO）。
 * bound 来自形式类型参数声明（签名 {@code <A:Lfoo/Bar;>}）;genericDeclaration
 * 为声明类的 Class 镜像。VM 接缝填字段构造。AnnotatedElement 面 M 级空语义。
 */
public class TypeVariableImpl<D extends GenericDeclaration> implements TypeVariable<D> {

    private String name;
    private GenericDeclaration genericDeclaration;
    private Type[] bounds;

    /** VM 直填字段（AllocObject 后布局写入）。 */
    public TypeVariableImpl() {}

    @Override
    public Type[] getBounds() {
        return bounds.clone();
    }

    @Override
    @SuppressWarnings("unchecked")
    public D getGenericDeclaration() {
        return (D) genericDeclaration;
    }

    @Override
    public String getName() {
        return name;
    }

    @Override
    public AnnotatedType[] getAnnotatedBounds() {
        return new AnnotatedType[0];
    }

    // ---- AnnotatedElement 面（M 级最小:无注解语义）----
    @Override
    public <T extends Annotation> T getAnnotation(Class<T> annotationClass) {
        return null;
    }

    @Override
    public Annotation[] getAnnotations() {
        return new Annotation[0];
    }

    @Override
    @SuppressWarnings("unchecked")
    public <T extends Annotation> T[] getAnnotationsByType(Class<T> annotationClass) {
        return (T[]) new Annotation[0];
    }

    @Override
    public <T extends Annotation> T getDeclaredAnnotation(Class<T> annotationClass) {
        return null;
    }

    @Override
    public Annotation[] getDeclaredAnnotations() {
        return new Annotation[0];
    }

    @Override
    @SuppressWarnings("unchecked")
    public <T extends Annotation> T[] getDeclaredAnnotationsByType(Class<T> annotationClass) {
        return (T[]) new Annotation[0];
    }

    @Override
    public boolean isAnnotationPresent(Class<? extends Annotation> annotationClass) {
        return false;
    }

    @Override
    public String getTypeName() {
        return name;
    }

    @Override
    public String toString() {
        return name;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof TypeVariable)) {
            return false;
        }
        TypeVariable<?> that = (TypeVariable<?>) o;
        boolean declEq = (genericDeclaration == null)
                ? that.getGenericDeclaration() == null
                : genericDeclaration.equals(that.getGenericDeclaration());
        return declEq && (name == null ? that.getName() == null : name.equals(that.getName()));
    }

    @Override
    public int hashCode() {
        return (genericDeclaration == null ? 0 : genericDeclaration.hashCode())
                ^ (name == null ? 0 : name.hashCode());
    }
}
