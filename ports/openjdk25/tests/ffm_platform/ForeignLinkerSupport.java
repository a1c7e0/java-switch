package jdk.internal.vm;

/** Forces the optimized-linker branch in the unpatched CABI for regression coverage. */
public final class ForeignLinkerSupport {
    private ForeignLinkerSupport() { }
    public static boolean isSupported() { return true; }
}
