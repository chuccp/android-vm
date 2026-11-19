package javax.security.auth;

public interface Destroyable {

    public default void destroy() throws DestroyFailedException {
        throw new DestroyFailedException();
    }

    public default boolean isDestroyed() {
        return false;
    }
}
