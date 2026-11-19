package java.lang;

import jdk.internal.reflect.ReflectionFactory;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.security.AccessController;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;

final class PublicMethods {

    private final Map<Key, MethodList> map = new LinkedHashMap<>();

    private int methodCount;

    void merge(Method method) {
        Key key = new Key(method);
        MethodList existing = map.get(key);
        int xLen = existing == null ? 0 : existing.length();
        MethodList merged = MethodList.merge(existing, method);
        methodCount += merged.length() - xLen;

        if (merged != existing) {
            map.put(key, merged);
        }
    }

    Method[] toArray() {
        Method[] array = new Method[methodCount];
        int i = 0;
        for (MethodList ml : map.values()) {
            for (; ml != null; ml = ml.next) {
                array[i++] = ml.method;
            }
        }
        return array;
    }

    private static final class Key {
        private static final ReflectionFactory reflectionFactory =
            AccessController.doPrivileged(
                new ReflectionFactory.GetReflectionFactoryAction());

        private final String name;
        private final Class<?>[] ptypes;

        Key(Method method) {
            name = method.getName();
            ptypes = reflectionFactory.getExecutableSharedParameterTypes(method);
        }

        static boolean matches(Method method,
                               String name,
                               Class<?>[] ptypes) {
            return method.getName().equals(name) &&
                   Arrays.equals(
                       reflectionFactory.getExecutableSharedParameterTypes(method),
                       ptypes
                   );
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (!(o instanceof Key)) return false;
            Key that = (Key) o;

            return name == that.name &&
                   Arrays.equals(ptypes, that.ptypes);
        }

        @Override
        public int hashCode() {
            return System.identityHashCode(name) +
                   31 * Arrays.hashCode(ptypes);
        }
    }

    static final class MethodList {
        Method method;
        MethodList next;

        private MethodList(Method method) {
            this.method = method;
        }

        static MethodList filter(Method[] methods, String name,
                                 Class<?>[] ptypes, boolean includeStatic) {
            MethodList head = null, tail = null;
            for (Method method : methods) {
                if ((includeStatic || !Modifier.isStatic(method.getModifiers())) &&
                    Key.matches(method, name, ptypes)) {
                    if (tail == null) {
                        head = tail = new MethodList(method);
                    } else {
                        tail = tail.next = new MethodList(method);
                    }
                }
            }
            return head;
        }

        static MethodList merge(MethodList head, MethodList methodList) {
            for (MethodList ml = methodList; ml != null; ml = ml.next) {
                head = merge(head, ml.method);
            }
            return head;
        }

        private static MethodList merge(MethodList head, Method method) {
            Class<?> dclass = method.getDeclaringClass();
            Class<?> rtype = method.getReturnType();
            MethodList prev = null;
            for (MethodList l = head; l != null; l = l.next) {

                Method xmethod = l.method;

                if (rtype == xmethod.getReturnType()) {
                    Class<?> xdclass = xmethod.getDeclaringClass();
                    if (dclass.isInterface() == xdclass.isInterface()) {

                        if (dclass.isAssignableFrom(xdclass)) {

                            return head;
                        }
                        if (xdclass.isAssignableFrom(dclass)) {

                            if (prev != null) {
                                prev.next = l.next;
                            } else {
                                head = l.next;
                            }

                        } else {

                            prev = l;

                        }
                    } else if (dclass.isInterface()) {

                        return head;
                    } else  {

                        if (prev != null) {
                            prev.next = l.next;
                        } else {
                            head = l.next;
                        }

                    }
                } else {

                    prev = l;

                }
            }

            if (prev == null) {
                head = new MethodList(method);
            } else {
                prev.next = new MethodList(method);
            }
            return head;
        }

        private int length() {
            int len = 1;
            for (MethodList ml = next; ml != null; ml = ml.next) {
                len++;
            }
            return len;
        }

        Method getMostSpecific() {
            Method m = method;
            Class<?> rt = m.getReturnType();
            for (MethodList ml = next; ml != null; ml = ml.next) {
                Method m2 = ml.method;
                Class<?> rt2 = m2.getReturnType();
                if (rt2 != rt && rt.isAssignableFrom(rt2)) {

                    m = m2;
                    rt = rt2;
                }
            }
            return m;
        }
    }
}
