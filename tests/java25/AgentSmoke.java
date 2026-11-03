public final class AgentSmoke {
    private static final String ORIGINAL = "sentinel-before-agent";

    private static String sentinel() {
        return ORIGINAL;
    }

    public static void main(String[] args) {
        require("sentinel-transformed-before-define".equals(sentinel()),
                "ClassFile API transformation did not affect the initial definition");
        require(args.length == 1 && args[0].equals("main-argument-forwarded"),
                "main argument was not forwarded");
        require("property-forwarded".equals(System.getProperty("agent.smoke.property")),
                "system property was not forwarded");
        require("premain-argument-forwarded".equals(System.getProperty("agent.smoke.argument")),
                "agent argument was not forwarded to premain");
        require("1".equals(System.getProperty("agent.smoke.premain.count")),
                "premain did not run exactly once");
        require("1".equals(System.getProperty("agent.smoke.transform.count")),
                "the sentinel class was not transformed exactly once");
        // These values are observations, not requirements: a supported JVM may
        // truthfully report either capability while this agent requests neither.
        System.out.println("instrumentation.redefine="
                + System.getProperty("agent.smoke.instrumentation.redefine"));
        System.out.println("instrumentation.retransform="
                + System.getProperty("agent.smoke.instrumentation.retransform"));
        System.out.println("instrumentation.nativePrefix="
                + System.getProperty("agent.smoke.instrumentation.nativePrefix"));
        System.out.println("JAVA25_AGENT_SMOKE_PASS");
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
