package android.content;

public final class ContextParams {
    private final String attributionTag;
    private final AttributionSource nextAttributionSource;
    private final boolean registerAttributionSource;

    private ContextParams(
            String attributionTag,
            AttributionSource nextAttributionSource,
            boolean registerAttributionSource) {
        this.attributionTag = attributionTag;
        this.nextAttributionSource = nextAttributionSource;
        this.registerAttributionSource = registerAttributionSource;
    }

    public native String getAttributionTag();

    public native AttributionSource getNextAttributionSource();

    public native boolean shouldRegisterAttributionSource();
}
