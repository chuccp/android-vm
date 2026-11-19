package java.lang.reflect;

public interface AnnotatedType extends AnnotatedElement {

    default AnnotatedType getAnnotatedOwnerType() {
        return null;
    }

    public Type getType();
}
