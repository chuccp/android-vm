package java.util;

/**
 * VM 链表遍历钩子（编译期脚手架）：实现由 libdexvm.so 内建类提供
 * （core/framework_collections.cc 注册 intrinsic；builtin_classes.def 排除
 * 本类的 Java 版本，双实现零容忍——本文件仅让 lib 源码可编译）。
 *
 * AOSP 口径：集合核心由 ART 运行时高效承接（JIT/AOT 后的 node 遍历近乎
 * 免费）。解释器形态下每个 node 步骤都要一次 DoCall + 帧分配，app 启动期
 * 的按 index 遍历（O(n²)）因此放大成秒级；遍历下沉到 C++ 承接。
 */
public final class VmLinkedList {
    private VmLinkedList() {}

    /** list.get(index)（越界抛 IndexOutOfBoundsException）。 */
    public static Object get(LinkedList<?> list, int index) {
        throw new UnsupportedOperationException("VmLinkedList.get: VM intrinsic");
    }

    /** list.node(index)（AOSP 双向就近遍历）。 */
    public static Object node(LinkedList<?> list, int index) {
        throw new UnsupportedOperationException("VmLinkedList.node: VM intrinsic");
    }
}
