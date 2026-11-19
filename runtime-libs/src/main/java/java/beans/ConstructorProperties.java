package java.beans;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * java.beans.ConstructorProperties 注解（JDK 6+ 面）。与 [[Transient]] 同批
 * 补齐：jackson-databind 的 Java7Support 反射面按名引用。
 */
@Target(ElementType.CONSTRUCTOR)
@Retention(RetentionPolicy.RUNTIME)
public @interface ConstructorProperties {
    String[] value();
}
