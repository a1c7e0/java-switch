package jdk.internal.util;

/** Horizon fixture for testing CABI initialization on the host JDK. */
public enum OperatingSystem {
    LINUX, MACOS, WINDOWS, AIX, HORIZON;

    public static OperatingSystem current() { return HORIZON; }
    public static boolean isLinux() { return false; }
    public static boolean isMacOS() { return false; }
    public static boolean isWindows() { return false; }
    public static boolean isAix() { return false; }
}
