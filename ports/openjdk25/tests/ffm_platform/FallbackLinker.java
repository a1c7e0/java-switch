package jdk.internal.foreign.abi.fallback;

/** Configurable fallback availability fixture for isolated CABI initialization. */
public final class FallbackLinker {
    private FallbackLinker() { }
    public static boolean isSupported() {
        return Boolean.getBoolean("test.fallback.supported");
    }
}
