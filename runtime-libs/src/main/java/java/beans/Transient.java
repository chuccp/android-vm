package java.beans;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * java.beans.Transient 注解（JDK 7+ 面）。jackson-databind 的 Java7Support
 * 实现类构造时 const-class 本注解；缺类会让 ObjectMapper 扩展初始化失败
 * （ext/b 无法实例化，进而在反序列化时抛 IllegalArgumentException）。
 */
@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface Transient {
}
