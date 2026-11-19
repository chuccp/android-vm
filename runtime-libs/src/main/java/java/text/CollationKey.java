package java.text;

public abstract class CollationKey implements Comparable<CollationKey> {

    public abstract int compareTo(CollationKey target);

    public String getSourceString() {
        return source;
    }

    public abstract byte[] toByteArray();

    protected CollationKey(String source) {
        if (source==null){
            throw new NullPointerException();
        }
        this.source = source;
    }

    private final String source;
}
