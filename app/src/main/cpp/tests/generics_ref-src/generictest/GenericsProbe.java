package generictest;

import java.lang.reflect.GenericArrayType;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.lang.reflect.TypeVariable;
import java.lang.reflect.WildcardType;

/**
 * 泛型反射面（Class.getGenericSuperclass/getGenericInterfaces/getTypeParameters）
 * 语义用例：按 dex Signature 属性解析出的 Type 镜像做断言。
 * 每行输出 "name: OK" 或 "name: FAIL ...";harness 按行判定。
 */
public class GenericsProbe {

    public static class Base<T> {}

    public static class Plain extends Base<String> {}

    public static class NonGeneric {}

    public static class TwoArg<A, B extends Number> extends Base<A> {}

    public interface Iface<T> {}

    public static class Impl implements Iface<Integer> {}

    public static class WildImpl implements Iface<java.util.List<? extends Number>> {}

    public static class GArr<T> implements Iface<T[]> {}

    private static StringBuilder out = new StringBuilder();

    private static void check(String name, boolean ok, String detail) {
        out.append(name).append(": ").append(ok ? "OK" : "FAIL ").append(ok ? "" : detail).append('\n');
    }

    public static String run() {
        out = new StringBuilder();

        // G1: 无类型变量的参数化超类 -> ParameterizedType(raw=Base, args=[String])
        {
            Type t = Plain.class.getGenericSuperclass();
            boolean ok = t instanceof ParameterizedType;
            String detail = "got " + t;
            if (ok) {
                ParameterizedType pt = (ParameterizedType) t;
                Type raw = pt.getRawType();
                Type[] args = pt.getActualTypeArguments();
                ok = raw == Base.class && args != null && args.length == 1 && args[0] == String.class;
                detail = "raw=" + raw + " args=" + java.util.Arrays.toString(args);
                if (ok) {
                    // 嵌套类 owner 语义（OpenJDK：getOwnerType()=封闭类）
                    ok = pt.getOwnerType() == GenericsProbe.class;
                    detail = "owner=" + pt.getOwnerType();
                }
            }
            check("genericSuperclass.param", ok, detail);
        }

        // G2: 无泛型超类 -> superclass.class；Object -> null
        {
            Type t = NonGeneric.class.getGenericSuperclass();
            boolean ok = t == Object.class;
            check("genericSuperclass.plain", ok, "got " + t);
            Type t2 = Object.class.getGenericSuperclass();
            check("genericSuperclass.objectNull", t2 == null, "got " + t2);
            Type t3 = Plain.class.getSuperclass() == Base.class ? Base.class.getGenericSuperclass() : null;
            check("genericSuperclass.basePlain", t3 == Object.class, "got " + t3);
        }

        // G3: 类型变量实参 -> TypeVariable(name=A)
        {
            Type t = TwoArg.class.getGenericSuperclass();
            boolean ok = t instanceof ParameterizedType;
            String detail = "got " + t;
            if (ok) {
                Type[] args = ((ParameterizedType) t).getActualTypeArguments();
                ok = args != null && args.length == 1 && args[0] instanceof TypeVariable;
                detail = "args=" + java.util.Arrays.toString(args);
                if (ok) {
                    TypeVariable<?> tv = (TypeVariable<?>) args[0];
                    ok = "A".equals(tv.getName()) && tv.getGenericDeclaration() == TwoArg.class;
                    detail = "name=" + tv.getName() + " decl=" + tv.getGenericDeclaration();
                }
            }
            check("genericSuperclass.typeVar", ok, detail);
        }

        // G4: getGenericInterfaces -> ParameterizedType(Iface,[Integer])
        {
            Type[] ts = Impl.class.getGenericInterfaces();
            boolean ok = ts != null && ts.length == 1 && ts[0] instanceof ParameterizedType;
            String detail = "got " + java.util.Arrays.toString(ts);
            if (ok) {
                ParameterizedType pt = (ParameterizedType) ts[0];
                Type[] args = pt.getActualTypeArguments();
                ok = pt.getRawType() == Iface.class && args != null && args.length == 1
                        && args[0] == Integer.class;
                detail = "raw=" + pt.getRawType() + " args=" + java.util.Arrays.toString(args);
            }
            check("genericInterfaces.param", ok, detail);
        }

        // G5: 嵌套通配符 -> Iface<List<? extends Number>>
        {
            Type[] ts = WildImpl.class.getGenericInterfaces();
            boolean ok = ts != null && ts.length == 1 && ts[0] instanceof ParameterizedType;
            String detail = "got " + java.util.Arrays.toString(ts);
            if (ok) {
                Type[] args = ((ParameterizedType) ts[0]).getActualTypeArguments();
                ok = args != null && args.length == 1 && args[0] instanceof ParameterizedType;
                detail = "args=" + java.util.Arrays.toString(args);
                if (ok) {
                    Type[] wargs = ((ParameterizedType) args[0]).getActualTypeArguments();
                    ok = wargs != null && wargs.length == 1 && wargs[0] instanceof WildcardType;
                    detail = "wargs=" + java.util.Arrays.toString(wargs);
                    if (ok) {
                        WildcardType w = (WildcardType) wargs[0];
                        Type[] upper = w.getUpperBounds();
                        Type[] lower = w.getLowerBounds();
                        ok = upper != null && upper.length == 1 && upper[0] == Number.class
                                && lower != null && lower.length == 0;
                        detail = "upper=" + java.util.Arrays.toString(upper)
                                + " lower=" + java.util.Arrays.toString(lower);
                    }
                }
            }
            check("genericInterfaces.wildcard", ok, detail);
        }

        // G6: 泛型数组实参 -> Iface<T[]>
        {
            Type[] ts = GArr.class.getGenericInterfaces();
            boolean ok = ts != null && ts.length == 1 && ts[0] instanceof ParameterizedType;
            String detail = "got " + java.util.Arrays.toString(ts);
            if (ok) {
                Type[] args = ((ParameterizedType) ts[0]).getActualTypeArguments();
                ok = args != null && args.length == 1 && args[0] instanceof GenericArrayType;
                detail = "args=" + java.util.Arrays.toString(args);
                if (ok) {
                    Type comp = ((GenericArrayType) args[0]).getGenericComponentType();
                    ok = comp instanceof TypeVariable && "T".equals(((TypeVariable<?>) comp).getName());
                    detail = "comp=" + comp;
                }
            }
            check("genericInterfaces.garray", ok, detail);
        }

        // G7: getTypeParameters（含 bound）
        {
            TypeVariable<?>[] tvs = TwoArg.class.getTypeParameters();
            boolean ok = tvs != null && tvs.length == 2;
            String detail = "len=" + (tvs == null ? -1 : tvs.length);
            if (ok) {
                ok = "A".equals(tvs[0].getName()) && "B".equals(tvs[1].getName());
                detail = "names=" + tvs[0].getName() + "," + tvs[1].getName();
                if (ok) {
                    Type[] b0 = tvs[0].getBounds();
                    Type[] b1 = tvs[1].getBounds();
                    ok = b0 != null && b0.length == 1 && b0[0] == Object.class
                            && b1 != null && b1.length == 1 && b1[0] == Number.class;
                    detail = "b0=" + java.util.Arrays.toString(b0)
                            + " b1=" + java.util.Arrays.toString(b1);
                }
            }
            check("typeParameters.bounds", ok, detail);
        }

        // G8: 无泛型类 -> 空数组；toString/getTypeName 形态
        {
            TypeVariable<?>[] tvs = NonGeneric.class.getTypeParameters();
            boolean ok = tvs != null && tvs.length == 0;
            check("typeParameters.empty", ok, "len=" + (tvs == null ? -1 : tvs.length));
            Type t = Plain.class.getGenericSuperclass();
            String s = String.valueOf(t);
            boolean ok2 = s != null && s.indexOf("Base") >= 0 && s.indexOf("String") >= 0;
            check("typeName.form", ok2, "got " + s);
        }

        // G9: 接口的 getGenericSuperclass 为 null（对齐 vendored Class.java 语义）
        {
            Type t = Iface.class.getGenericSuperclass();
            check("genericSuperclass.ifaceNull", t == null, "got " + t);
        }

        return out.toString();
    }
}
