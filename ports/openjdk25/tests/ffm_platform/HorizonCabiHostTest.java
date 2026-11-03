package jdk.internal.foreign;

public class HorizonCabiHostTest {
    public static void main(String[] args) {
        if (args.length != 1) {
            throw new AssertionError("expected CABI enum name");
        }
        CABI expected = CABI.valueOf(args[0]);
        if (CABI.current() != expected) {
            throw new AssertionError("expected " + expected + " but got " + CABI.current());
        }
    }
}
