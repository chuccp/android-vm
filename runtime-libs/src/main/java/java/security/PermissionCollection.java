package java.security;

import java.util.*;
import java.util.stream.Stream;
import java.util.stream.StreamSupport;

public abstract class PermissionCollection implements java.io.Serializable {

    private static final long serialVersionUID = -6727011328946861783L;

    private volatile boolean readOnly;

    public abstract void add(Permission permission);

    public abstract boolean implies(Permission permission);

    public abstract Enumeration<Permission> elements();

    public Stream<Permission> elementsAsStream() {
        int characteristics = isReadOnly()
                ? Spliterator.NONNULL | Spliterator.IMMUTABLE
                : Spliterator.NONNULL;
        return StreamSupport.stream(
                Spliterators.spliteratorUnknownSize(
                        elements().asIterator(), characteristics),
                false);
    }

    public void setReadOnly() {
        readOnly = true;
    }

    public boolean isReadOnly() {
        return readOnly;
    }

    public String toString() {
        Enumeration<Permission> enum_ = elements();
        StringBuilder sb = new StringBuilder();
        sb.append(super.toString()+" (\n");
        while (enum_.hasMoreElements()) {
            try {
                sb.append(" ");
                sb.append(enum_.nextElement().toString());
                sb.append("\n");
            } catch (NoSuchElementException e){

            }
        }
        sb.append(")\n");
        return sb.toString();
    }
}
