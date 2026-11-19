package jdk.internal.org.objectweb.asm;

public class Label {
    public Label() {
    }

    public String toString() {
        return "L" + System.identityHashCode(this);
    }
}
